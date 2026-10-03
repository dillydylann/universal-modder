-- A Minecraft mob (or lit TNT) as a GMod entity. It follows Minecraft's position every tick, and only becomes a
-- physics object while the physgun or gravity gun holds it. Damage done to it here is sent to Minecraft, which
-- owns its health.
AddCSLuaFile()

ENT.Type = "anim"
ENT.Base = "base_anim"
ENT.PrintName = "Minecraft mob"
ENT.Category = "Minecraft"
ENT.Spawnable = false
ENT.RenderGroup = RENDERGROUP_OPAQUE

function ENT:SetupDataTables()
	self:NetworkVar("String", 0, "Kind")
	self:NetworkVar("Int", 0, "McId")
	self:NetworkVar("Float", 0, "W")
	self:NetworkVar("Float", 1, "H")
	self:NetworkVar("Float", 2, "Hp")
	self:NetworkVar("Float", 3, "MaxHp")
	self:NetworkVar("Float", 4, "HurtTime")
end

function ENT:Bounds()
	local s = MCB.Scale()
	local hw, h = math.max(self:GetW(), 0.3) * s * 0.5, math.max(self:GetH(), 0.3) * s
	return Vector(-hw, -hw, 0), Vector(hw, hw, h)
end

if CLIENT then
	function ENT:Initialize()
		local lo, hi = self:Bounds()
		self:SetRenderBounds(lo - Vector(8, 8, 8), hi + Vector(8, 8, 8))
	end

	function ENT:Draw()
		MCB.DrawMob(self)
	end

	return
end

function ENT:Setup(id, kind, w, h, hostile)
	self:SetMcId(id)
	self:SetKind(kind)
	self:SetW(w)
	self:SetH(h)
	self.Hostile = hostile
end

function ENT:Initialize()
	-- any model will do: it's never drawn, and the physics box below replaces its collision
	self:SetModel("models/hunter/blocks/cube025x025x025.mdl")
	local lo, hi = self:Bounds()
	self:PhysicsInitBox(lo, hi)
	self:SetCollisionBounds(lo, hi)
	self:SetMoveType(MOVETYPE_VPHYSICS)
	self:SetSolid(SOLID_VPHYSICS)
	-- bullets and the physgun hit it; players and NPCs walk through it (Minecraft does the pushing)
	self:SetCollisionGroup(COLLISION_GROUP_WEAPON)
	self:DrawShadow(false)

	local phys = self:GetPhysicsObject()
	if IsValid(phys) then
		phys:SetMaterial(self:GetKind() == "tnt" and "wood" or "zombieflesh")
		phys:SetMass(math.Clamp(self:GetW() * self:GetW() * self:GetH() * 120, 5, 1000))
		phys:EnableMotion(false)
	end

	-- hostile mobs get an invisible npc_bullseye so GMod's NPCs fight them (sv_mobs sets the relationships)
	if self.Hostile then
		local eye = ents.Create("npc_bullseye")
		if IsValid(eye) then
			eye:SetPos(self:LocalToWorld(Vector(0, 0, hi.z * 0.6)))
			eye:SetKeyValue("spawnflags", tostring(65536 + 131072)) -- not solid, take no damage
			eye:SetKeyValue("health", "9999")
			eye:Spawn()
			eye:Activate()
			eye:SetParent(self)
			eye:SetNotSolid(true)
			self:DeleteOnRemove(eye)
			self.Bullseye = eye
			MCB.MakeHated(eye)
		end
	end
end

function ENT:SetMCHealth(hp, max)
	if hp < self:GetHp() then self:SetHurtTime(CurTime()) end
	self:SetHp(hp)
	self:SetMaxHp(max)
end

--- Minecraft moved it: frozen in place, where Minecraft says.
function ENT:FollowMC(pos, yaw)
	local phys = self:GetPhysicsObject()
	if IsValid(phys) and phys:IsMotionEnabled() then phys:EnableMotion(false) end
	self:SetPos(pos)
	self:SetAngles(Angle(0, yaw, 0))
end

function ENT:Grabbed(ply)
	self.MCHeldBy = ply
	local phys = self:GetPhysicsObject()
	if IsValid(phys) then
		phys:EnableMotion(true)
		phys:Wake()
	end
end

--- Let go: Minecraft takes it back at the velocity it was thrown with.
function ENT:Dropped()
	if not self.MCHeldBy then return end
	self.MCHeldBy = nil
	local phys = self:GetPhysicsObject()
	local v = IsValid(phys) and phys:GetVelocity() or Vector(0, 0, 0)
	local vx, vy, vz = MCB.VelToMC(v)
	MCB.Queue("release", { self:GetMcId(), vx, vy, vz })
	-- frozen until Minecraft reports it again
	if IsValid(phys) then phys:EnableMotion(false) end
end

function ENT:Think()
	local by = self.MCHeldBy
	if by ~= nil and (not IsValid(by) or (by:IsPlayer() and not by:Alive())) then self:Dropped() end
end

local function kindOf(t)
	if bit.band(t, DMG_BURN + DMG_SLOWBURN) ~= 0 then return "fire" end
	if bit.band(t, DMG_BULLET + DMG_BUCKSHOT + DMG_AIRBOAT) ~= 0 then return "bullet" end
	if bit.band(t, DMG_CRUSH + DMG_FALL) ~= 0 then return "crush" end
	return "melee"
end

function ENT:Hurt(amount, attacker, kind)
	if self:GetKind() == "tnt" then return end
	amount = amount / MCB.cvHp:GetFloat()
	if amount <= 0 then return end
	MCB.Queue("dmg", { self:GetMcId(), amount, IsValid(attacker) and attacker:EntIndex() or 0, kind })
	self:SetHurtTime(CurTime())
end

function ENT:OnTakeDamage(dmg)
	local t = dmg:GetDamageType()
	-- every explosion near Minecraft happens in Minecraft too, and hurts the mob there: don't count it twice
	if bit.band(t, DMG_BLAST) ~= 0 then return end
	self:Hurt(dmg:GetDamage(), dmg:GetAttacker(), kindOf(t))
end

-- props thrown at it, or the mob slammed into things with the physgun
function ENT:PhysicsCollide(data)
	local speed = data.Speed
	if speed < 350 or (self.NextCrush or 0) > CurTime() then return end
	local other = data.HitEntity
	local mass = 50
	if IsValid(data.HitObject) and not (IsValid(other) and other:IsWorld()) then mass = data.HitObject:GetMass() end
	if not self.MCHeldBy and (not IsValid(other) or other:IsWorld()) then return end

	self.NextCrush = CurTime() + 0.3
	local amount = (speed - 350) / 25 * math.Clamp(mass / 50, 0.3, 4)
	local attacker = self.MCHeldBy
	if not IsValid(attacker) and IsValid(other) then
		attacker = other.GetPhysicsAttacker and other:GetPhysicsAttacker(2) or nil
		if not IsValid(attacker) then attacker = other end
	end
	self:Hurt(amount, attacker, "crush")
end

function ENT:UpdateTransmitState()
	return TRANSMIT_PVS
end
