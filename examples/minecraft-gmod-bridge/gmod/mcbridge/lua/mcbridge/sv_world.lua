-- Server: the map <-> Minecraft's world.
--  * The GMod map around the player is voxelised into Minecraft blocks: solid cells become barriers there (so mobs
--    walk on GMod floors and stop at its walls) and the map's water becomes Minecraft water (so mobs swim in it).
--  * Minecraft's own blocks become solid mc_block entities here (you walk on them; the physgun lifts them), and its
--    water is mirrored for swimming (see sh_core.lua) and drawn by the clients.
--  * Explosions go both ways: GMod rockets, grenades and explosive barrels blow up in Minecraft (breaking blocks,
--    hurting mobs, setting off TNT); Minecraft TNT and creepers blow up in GMod.

local SCAN_R, SCAN_DOWN, SCAN_UP = 16, 8, 12
local TRACE_BUDGET = 160 -- hull traces per tick
local BLOCK_R = 24 -- Minecraft blocks within this many blocks of the player exist as entities
local MAX_BLOCKS = 2500
local EXPLOSIVES = {
	rpg_missile = 3, -- Minecraft power (TNT is 4, a creeper 3)
	grenade_ar2 = 2.5,
	npc_grenade_frag = 2.5,
	grenade_helicopter = 3,
	npc_satchel = 3,
	npc_tripmine = 3,
}

MCB.Known = MCB.Known or {} -- key -> block name, every Minecraft block reported
MCB.BlockEnts = MCB.BlockEnts or {} -- key -> mc_block entity

-- ---- scanning the GMod map ----

local OFFSETS = {}
for dx = -SCAN_R, SCAN_R do
	for dz = -SCAN_R, SCAN_R do
		for dy = -SCAN_DOWN, SCAN_UP do
			OFFSETS[#OFFSETS + 1] = { dx, dy, dz, dx * dx + dz * dz + dy * dy * 2 }
		end
	end
end
table.sort(OFFSETS, function(a, b) return a[4] < b[4] end)

local scanned = {} -- key -> 0 air, 1 solid, 2 water
local pendingWater = {} -- key -> true: map water not sent yet (waiting for its neighbours)
local cursor, cx, cy, cz = 1

--- What the map has in this block: 1 solid (world brushes, displacements, static props), 2 water, 0 air.
local function probe(bx, by, bz)
	local c = MCB.CellCenter(bx, by, bz)
	local h = MCB.Scale() * 0.4
	local filter = {}
	for _ = 1, 4 do
		local tr = util.TraceHull({
			start = c, endpos = c,
			mins = Vector(-h, -h, -h), maxs = Vector(h, h, h),
			mask = MASK_PLAYERSOLID, filter = filter,
		})
		if not tr.Hit and not tr.StartSolid then break end
		if tr.HitWorld or not IsValid(tr.Entity) then return 1 end
		filter[#filter + 1] = tr.Entity -- a prop, player or NPC: they move, so they aren't map
	end

	if bit.band(util.PointContents(c), CONTENTS_WATER) ~= 0 then return 2 end
	return 0
end

local NEIGHBOURS = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 } }

--- Map water goes over once its sides and bottom are known to be water or solid: a cell at the edge of the scan
--- would otherwise spill Minecraft water out into the room.
local function flushWater()
	for k in pairs(pendingWater) do
		local x, y, z = MCB.Unkey(k)
		local ready, leaks = true, false
		for _, n in ipairs(NEIGHBOURS) do
			local s = scanned[MCB.Key(x + n[1], y + n[2], z + n[3])]
			if s == nil then
				ready = false
			elseif s == 0 then
				leaks = true
			end
		end

		if leaks then
			pendingWater[k] = nil
		elseif ready then
			pendingWater[k] = nil
			MCB.QueueFlat("water", x, y, z)
		end
	end
end

hook.Add("Tick", "mcb_scan", function()
	if not MCB.Connected then return end
	local host = MCB.Host()
	if not IsValid(host) then return end

	local x, y, z = MCB.CellOf(host:GetPos())
	if x ~= cx or y ~= cy or z ~= cz then
		cx, cy, cz, cursor = x, y, z, 1
	end

	local traces, steps = 0, 0
	while cursor <= #OFFSETS and traces < TRACE_BUDGET and steps < 6000 do
		local o = OFFSETS[cursor]
		cursor, steps = cursor + 1, steps + 1
		local bx, by, bz = cx + o[1], cy + o[2], cz + o[3]
		if by >= -64 and by < 320 then
			local k = MCB.Key(bx, by, bz)
			if scanned[k] == nil then
				traces = traces + 1
				local kind = probe(bx, by, bz)
				scanned[k] = kind
				if kind == 1 then
					MCB.QueueFlat("solid", bx, by, bz)
				elseif kind == 2 then
					pendingWater[k] = true
				end
			end
		end
	end

	if traces > 0 and next(pendingWater) then flushWater() end
end)

-- ---- connecting: pick the vertical offset, start from a clean slate ----

local function removeBlockEnts()
	for k, ent in pairs(MCB.BlockEnts) do
		if IsValid(ent) then ent:Remove() end
		MCB.BlockEnts[k] = nil
	end
end

--- Start over: Minecraft drops the map cells it has, and the map is scanned again. `relevel` (or the first time on
--- a map) also picks the vertical offset again, from where the player stands now.
function MCB.ResetWorld(relevel)
	local host = MCB.Host()
	if IsValid(host) and (relevel or not GetGlobalBool("mcb_levelled")) then
		SetGlobalInt("mcb_yoff", 64 - math.floor(host:GetPos().z / MCB.Scale()))
		SetGlobalBool("mcb_levelled", true)
	end

	scanned, pendingWater, cursor, cx, cy, cz = {}, {}, 1, nil, nil, nil
	MCB.Known = {}
	removeBlockEnts()
	local dry = {}
	for k in pairs(MCB.Water) do dry[k] = 0 end
	MCB.SetWater(dry)
	MCB.Set("clear", true)
	MCB.Set("sync", 32)
end

hook.Add("MCBridgeConnected", "mcb_world", function() MCB.ResetWorld(false) end)
hook.Add("MCBridgeDisconnected", "mcb_world", function()
	scanned, pendingWater = {}, {}
end)

concommand.Add("mcbridge_reset", function(ply)
	if IsValid(ply) and not ply:IsSuperAdmin() and not game.SinglePlayer() then return end
	MCB.ResetWorld(true)
	print("[mcbridge] world reset: Minecraft y = 64 is where you stand; rescanning the map around you")
end)

hook.Add("MCBridgePoll", "mcb_world", function(host)
	local x, y, z = MCB.ToMC(host:GetPos())
	local ang = host:EyeAngles()
	MCB.Set("player", { x, y, z, MCB.YawToMC(ang.y), ang.p })
end)

-- ---- Minecraft's blocks and water ----

MCB.Handlers.blocks = function(e)
	for _, b in ipairs(e.set or {}) do
		MCB.Known[MCB.Key(b[1], b[2], b[3])] = b[4]
		local ent = MCB.BlockEnts[MCB.Key(b[1], b[2], b[3])]
		if IsValid(ent) then ent:SetBlock(b[4]) end
	end

	local clear = e.clear or {}
	for i = 1, #clear - 2, 3 do
		local k = MCB.Key(clear[i], clear[i + 1], clear[i + 2])
		MCB.Known[k] = nil
		local ent = MCB.BlockEnts[k]
		if IsValid(ent) then ent:Remove() end
		MCB.BlockEnts[k] = nil
	end

	local water = {}
	local any = false
	for _, w in ipairs(e.water or {}) do
		local k = MCB.Key(w[1], w[2], w[3])
		if (MCB.Water[k] or 0) ~= w[4] then
			water[k] = w[4]
			any = true
		end
	end

	if any then MCB.SetWater(water) end
end

MCB.Handlers.putfail = function(e)
	print("[mcbridge] no room for " .. tostring(e.b) .. " there")
end

--- Blocks near the player exist as entities; far ones only in MCB.Known (and in Minecraft).
timer.Create("mcb_blocks", 0.5, 0, function()
	local host = MCB.Host()
	if not IsValid(host) then return end

	local px, py, pz = MCB.CellOf(host:GetPos())
	local r2 = BLOCK_R * BLOCK_R
	local count = 0
	for k, ent in pairs(MCB.BlockEnts) do
		local x, y, z = MCB.Unkey(k)
		if not IsValid(ent) or MCB.Known[k] == nil or (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2 > (BLOCK_R + 6) ^ 2 then
			if IsValid(ent) then ent:Remove() end
			MCB.BlockEnts[k] = nil
		else
			count = count + 1
		end
	end

	local created = 0
	for k, name in pairs(MCB.Known) do
		if count >= MAX_BLOCKS or created >= 300 then break end
		if not MCB.BlockEnts[k] then
			local x, y, z = MCB.Unkey(k)
			if (x - px) ^ 2 + (y - py) ^ 2 + (z - pz) ^ 2 <= r2 then
				local ent = ents.Create("mc_block")
				if IsValid(ent) then
					ent:SetCell(x, y, z, name)
					ent:Spawn()
					MCB.BlockEnts[k] = ent
					count, created = count + 1, created + 1
				end
			end
		end
	end
end)

--- A static block was grabbed: it leaves Minecraft's world (quietly) and becomes a loose physics object here.
function MCB.TakeBlock(ent)
	local x, y, z = ent:GetCellX(), ent:GetCellY(), ent:GetCellZ()
	local k = MCB.Key(x, y, z)
	if MCB.BlockEnts[k] == ent then MCB.BlockEnts[k] = nil end
	MCB.Known[k] = nil
	MCB.QueueFlat("take", x, y, z)
end

--- A loose block came to rest: it goes back into Minecraft at the cell it's in (Minecraft reports it, and it
--- comes back as a static block).
function MCB.PutBlock(ent)
	local x, y, z = MCB.CellOf(ent:WorldSpaceCenter())
	MCB.Queue("put", { x, y, z, ent:GetBlock() })
	ent:Remove()
end

-- ---- explosions ----

MCB.Handlers.explosion = function(e)
	local pos = MCB.ToGM(e.pos[1], e.pos[2], e.pos[3])
	local power = tonumber(e.r) or 4
	local s = MCB.Scale()
	local world = game.GetWorld()
	-- Minecraft hurts within 2 x power blocks; TNT (4) does about what an RPG does in GMod at the centre
	util.BlastDamage(world, world, pos, power * 2 * s, power * 35)
	local fx = EffectData()
	fx:SetOrigin(pos)
	fx:SetMagnitude(power)
	fx:SetScale(power)
	util.Effect("Explosion", fx, true, true)
	util.ScreenShake(pos, 8, 5, 1, power * 6 * s)
end

local cleaning = false
hook.Add("PreCleanupMap", "mcb_explosions", function() cleaning = true end)
hook.Add("PostCleanupMap", "mcb_explosions", function() cleaning = false end)

local function explodeInMinecraft(pos, power)
	local host = MCB.Host()
	if cleaning or not MCB.Connected or not IsValid(host) then return end
	if host:GetPos():DistToSqr(pos) > (64 * MCB.Scale()) ^ 2 then return end

	local x, y, z = MCB.ToMC(pos)
	MCB.Queue("explode", { x, y, z, power })
end

hook.Add("EntityRemoved", "mcb_explosions", function(ent)
	local power = EXPLOSIVES[ent:GetClass()]
	if power then explodeInMinecraft(ent:GetPos(), power) end
end)

hook.Add("PropBreak", "mcb_explosions", function(_, prop)
	local model = string.lower(prop:GetModel() or "")
	if model:find("explosive", 1, true) or model:find("gascan", 1, true) or model:find("propane", 1, true) then
		explodeInMinecraft(prop:WorldSpaceCenter(), 3)
	end
end)

-- ---- Minecraft water acting on GMod things: props float, fire goes out ----

timer.Create("mcb_buoyancy", 0.1, 0, function()
	if not next(MCB.Water) then return end
	local host = MCB.Host()
	if not IsValid(host) then return end

	local dt = 0.1
	local g = GetConVarNumber("sv_gravity")
	for _, ent in ipairs(ents.FindInSphere(host:GetPos(), 40 * MCB.Scale())) do
		local center = ent:WorldSpaceCenter()
		if MCB.WaterAt(center) then
			if ent:IsOnFire() then ent:Extinguish() end

			local phys = ent:GetMoveType() == MOVETYPE_VPHYSICS and ent:GetPhysicsObject()
			local class = ent:GetClass()
			if phys and IsValid(phys) and phys:IsMotionEnabled() and class ~= "mc_mob" then
				-- wood and plastic float, Minecraft blocks (stone, mostly) sink slowly
				local lift = class == "mc_block" and 0.6 or 1.4
				phys:Wake()
				phys:ApplyForceCenter(Vector(0, 0, phys:GetMass() * g * lift * dt))
				phys:AddVelocity(-phys:GetVelocity() * math.min(1, 2 * dt))
			end
		end
	end
end)
