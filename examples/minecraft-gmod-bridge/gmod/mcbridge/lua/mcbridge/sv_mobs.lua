-- Server: Minecraft's mobs <-> GMod's weapons, NPCs, player and physgun.
--  * Every Minecraft mob (and lit TNT) near the player is a solid mc_mob entity that follows Minecraft's simulation:
--    bullets, crowbars, thrown props and fire hurt it, and the damage goes to the real mob in Minecraft.
--  * The player and every NPC are listed to Minecraft, where an invisible stand-in follows each; Minecraft's
--    hostile mobs hunt those, and their hits (claws, arrows, fire, drowning) come back off the real health here.
--  * GMod NPCs fight hostile mobs back: each one carries an npc_bullseye they're told to hate.
--  * The physgun and gravity gun pick mobs up: Minecraft hangs the mob where it's held and takes over again,
--    with the throw's velocity (and fall damage), when it's let go.

local PED_RANGE = 64 -- blocks

MCB.Mobs = MCB.Mobs or {} -- Minecraft entity id -> mc_mob

local function isPerson(ent)
	if ent:IsPlayer() then return ent:Alive() end
	return (ent:IsNPC() or ent:IsNextBot()) and ent:GetClass() ~= "npc_bullseye" and ent:Health() > 0
end

hook.Add("MCBridgePoll", "mcb_mobs", function(host)
	local peds = {}
	for _, ent in ipairs(ents.FindInSphere(host:GetPos(), PED_RANGE * MCB.Scale())) do
		if isPerson(ent) then
			local x, y, z = MCB.ToMC(ent:GetPos())
			peds[#peds + 1] = { ent:EntIndex(), x, y, z }
		end
	end

	MCB.Set("peds", peds)

	for id, mob in pairs(MCB.Mobs) do
		if IsValid(mob) and mob.MCHeldBy then
			local x, y, z = MCB.ToMC(mob:WorldSpaceCenter() - Vector(0, 0, mob:GetH() * MCB.Scale() * 0.5))
			MCB.Queue("hold", { id, x, y, z })
		end
	end
end)

--- The mob list in every reply: [id, kind, x, y, z, yaw, width, height, health, max health, held, hostile].
function MCB.OnMobs(list)
	local seen = {}
	for _, m in ipairs(list) do
		local id, kind = m[1], m[2]
		seen[id] = true
		local pos = MCB.ToGM(m[3], m[4], m[5])
		local ent = MCB.Mobs[id]
		if not IsValid(ent) then
			ent = ents.Create("mc_mob")
			if IsValid(ent) then
				ent:Setup(id, kind, m[7], m[8], m[12] == 1)
				ent:SetPos(pos)
				ent:SetAngles(Angle(0, MCB.YawToGM(m[6]), 0))
				ent:Spawn()
				MCB.Mobs[id] = ent
			end
		end

		if IsValid(ent) then
			ent:SetMCHealth(m[9], m[10])
			-- held = 1 while Minecraft hasn't heard the physgun let go: GMod's physics keeps it meanwhile
			if not ent.MCHeldBy and m[11] ~= 1 then
				ent:FollowMC(pos, MCB.YawToGM(m[6]))
			end
		end
	end

	for id, ent in pairs(MCB.Mobs) do
		if not seen[id] then
			if IsValid(ent) then ent:Remove() end
			MCB.Mobs[id] = nil
		end
	end
end

hook.Add("MCBridgeDisconnected", "mcb_mobs", function()
	MCB.OnMobs({})
end)

-- ---- Minecraft mobs hurting GMod's people ----

MCB.Handlers.mobhit = function(e)
	local target = Entity(tonumber(e.h) or 0)
	if not IsValid(target) or not isPerson(target) then return end
	-- GMod already drowns people in its own water
	if e.k == "drown" and target:WaterLevel() >= 3 then return end

	local attacker = MCB.Mobs[e.id]
	if not IsValid(attacker) then attacker = game.GetWorld() end

	local kind = DMG_SLASH
	if e.k == "fire" then
		kind = DMG_BURN
	elseif e.k == "drown" then
		kind = DMG_DROWN
	elseif e.proj then
		kind = DMG_BULLET
	end

	local from = MCB.ToGM(e.from[1], e.from[2], e.from[3])
	local dir = target:WorldSpaceCenter() - from
	dir.z = 0
	dir:Normalize()

	local d = DamageInfo()
	d:SetDamage((tonumber(e.d) or 0) * MCB.cvHp:GetFloat())
	d:SetDamageType(kind)
	d:SetAttacker(attacker)
	d:SetInflictor(attacker)
	d:SetDamagePosition(target:WorldSpaceCenter())
	d:SetDamageForce(dir * 3000)
	target:TakeDamageInfo(d)

	-- Minecraft's knockback
	if target:IsPlayer() and kind ~= DMG_BURN and kind ~= DMG_DROWN then
		target:SetVelocity(dir * 220 + Vector(0, 0, 160))
	end
end

-- ---- NPCs vs hostile mobs ----

local function hates(npc)
	return IsValid(npc) and npc:IsNPC() and npc:GetClass() ~= "npc_bullseye"
end

--- Every NPC hates this mob's bullseye (called when a hostile mob appears).
function MCB.MakeHated(bullseye)
	for _, npc in ipairs(ents.GetAll()) do
		if hates(npc) then npc:AddEntityRelationship(bullseye, D_HT, 50) end
	end
end

hook.Add("OnEntityCreated", "mcb_npcs", function(ent)
	timer.Simple(0, function()
		if not hates(ent) then return end
		for _, mob in pairs(MCB.Mobs) do
			if IsValid(mob) and IsValid(mob.Bullseye) then ent:AddEntityRelationship(mob.Bullseye, D_HT, 50) end
		end
	end)
end)

-- ---- physgun and gravity gun ----

local function ours(ent)
	local class = IsValid(ent) and ent:GetClass()
	return class == "mc_mob" or class == "mc_block"
end

hook.Add("PhysgunPickup", "mcb_grab", function(_, ent)
	if ours(ent) then return true end
end)

hook.Add("OnPhysgunPickup", "mcb_grab", function(ply, ent)
	if ours(ent) then ent:Grabbed(ply) end
end)

hook.Add("PhysgunDrop", "mcb_grab", function(_, ent)
	if ours(ent) then ent:Dropped() end
end)

hook.Add("GravGunPickupAllowed", "mcb_grab", function(_, ent)
	if ours(ent) then return true end
end)

hook.Add("GravGunOnPickedUp", "mcb_grab", function(ply, ent)
	if ours(ent) then ent:Grabbed(ply) end
end)

hook.Add("GravGunOnDropped", "mcb_grab", function(_, ent)
	if ours(ent) then ent:Dropped() end
end)

hook.Add("GravGunPunt", "mcb_grab", function(ply, ent)
	if not ours(ent) then return end
	if not ent.MCHeldBy then ent:Grabbed(ply) end
	-- after the punt has set the velocity
	timer.Simple(0, function()
		if IsValid(ent) then ent:Dropped() end
	end)
end)

-- ---- commands ----

local function allowed(ply)
	return not IsValid(ply) or ply:IsAdmin() or game.SinglePlayer()
end

concommand.Add("mcbridge_spawn", function(ply, _, args)
	if not allowed(ply) then return end
	local kind = string.lower(args[1] or "zombie")
	if not kind:match("^[a-z0-9_]+$") then return end

	local who = IsValid(ply) and ply or MCB.Host()
	if not IsValid(who) then return end

	local tr = who:GetEyeTrace()
	local s = MCB.Scale()
	for _ = 1, math.Clamp(tonumber(args[2]) or 1, 1, 20) do
		local pos = tr.HitPos + tr.HitNormal * 4 + Vector(math.Rand(-1.5, 1.5) * s, math.Rand(-1.5, 1.5) * s, 0)
		local x, y, z = MCB.ToMC(pos)
		MCB.Queue("spawn", { kind, x, y, z })
	end
end, function(cmd, argStr)
	local out = {}
	for _, k in ipairs({ "zombie", "skeleton", "creeper", "spider", "husk", "drowned", "stray", "witch", "slime", "enderman", "pillager", "vindicator", "cow", "pig", "sheep", "chicken", "wolf", "villager", "iron_golem" }) do
		if k:find(string.Trim(argStr), 1, true) then out[#out + 1] = cmd .. " " .. k end
	end

	return out
end)

concommand.Add("mcbridge_clearmobs", function(ply)
	if not allowed(ply) then return end
	MCB.Set("clearmobs", true)
end)
