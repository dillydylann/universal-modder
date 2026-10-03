-- A Minecraft block near the player, as a GMod entity: solid to players, props and bullets, and grabbable with the
-- physgun. Static blocks are drawn in batches by cl_render; a grabbed block is taken out of Minecraft and becomes a
-- loose physics cube until it comes to rest (or is frozen), then it's put back into Minecraft where it landed.
AddCSLuaFile()

ENT.Type = "anim"
ENT.Base = "base_anim"
ENT.PrintName = "Minecraft block"
ENT.Category = "Minecraft"
ENT.Spawnable = false
ENT.RenderGroup = RENDERGROUP_OPAQUE

function ENT:SetupDataTables()
	self:NetworkVar("String", 0, "Block")
	self:NetworkVar("Int", 0, "CellX")
	self:NetworkVar("Int", 1, "CellY")
	self:NetworkVar("Int", 2, "CellZ")
	self:NetworkVar("Bool", 0, "Loose")
end

if CLIENT then
	function ENT:Initialize()
		local h = MCB.Scale() * 0.5 + 2
		self:SetRenderBounds(Vector(-h, -h, -h), Vector(h, h, h))
	end

	function ENT:Draw()
		-- static blocks are part of cl_render's batched meshes
		if self:GetLoose() then MCB.DrawBlockAt(self, self:GetBlock(), 1) end
	end

	return
end

local function surfaceFor(name)
	if name:find("glass", 1, true) then return "glass" end
	if name:find("planks", 1, true) or name:find("log", 1, true) or name:find("wood", 1, true) or name == "tnt" then return "wood" end
	if name:find("leaves", 1, true) or name:find("wool", 1, true) then return "cardboard" end
	if name:find("sand", 1, true) or name:find("gravel", 1, true) then return "gravel" end
	if name:find("dirt", 1, true) or name:find("grass", 1, true) or name == "farmland" or name == "mud" then return "dirt" end
	if name:find("iron", 1, true) or name:find("gold", 1, true) or name:find("copper", 1, true) then return "metal" end
	return "concrete"
end

function ENT:SetCell(x, y, z, name)
	self:SetCellX(x)
	self:SetCellY(y)
	self:SetCellZ(z)
	self:SetBlock(name)
	self:SetPos(MCB.CellCenter(x, y, z))
	self:SetAngles(Angle(0, 0, 0))
end

function ENT:Initialize()
	self:SetModel("models/hunter/blocks/cube025x025x025.mdl")
	local h = MCB.Scale() * 0.5
	self:PhysicsInitBox(Vector(-h, -h, -h), Vector(h, h, h))
	self:SetCollisionBounds(Vector(-h, -h, -h), Vector(h, h, h))
	self:SetMoveType(MOVETYPE_VPHYSICS)
	self:SetSolid(SOLID_VPHYSICS)
	self:SetUseType(SIMPLE_USE)
	self:DrawShadow(false)

	local phys = self:GetPhysicsObject()
	if IsValid(phys) then
		phys:SetMaterial(surfaceFor(self:GetBlock()))
		phys:SetMass(60)
		phys:EnableMotion(false)
	end
end

-- static blocks are drawn from the client's list of every mc_block, so they must exist on the client even out of
-- view
function ENT:UpdateTransmitState()
	return TRANSMIT_ALWAYS
end

function ENT:Grabbed(ply)
	if not self:GetLoose() then
		MCB.TakeBlock(self)
		self:SetLoose(true)
	end

	self.MCHeldBy = ply
	self.RestSince = nil
	local phys = self:GetPhysicsObject()
	if IsValid(phys) then
		phys:EnableMotion(true)
		phys:Wake()
	end
end

function ENT:Dropped()
	self.MCHeldBy = nil
	self.DroppedAt = CurTime()
	self.RestSince = nil
end

function ENT:Think()
	if not self:GetLoose() then return end

	local by = self.MCHeldBy
	if by ~= nil and (not IsValid(by) or (by:IsPlayer() and not by:Alive())) then self:Dropped() end

	if not self.MCHeldBy then
		local phys = self:GetPhysicsObject()
		local now = CurTime()
		if not IsValid(phys) or not phys:IsMotionEnabled() then
			-- frozen with the physgun's right click: it goes back right there
			MCB.PutBlock(self)
			return
		end

		if phys:GetVelocity():Length() < 15 and phys:GetAngleVelocity():Length() < 30 then
			self.RestSince = self.RestSince or now
		else
			self.RestSince = nil
		end

		if (self.RestSince and now - self.RestSince > 0.6) or now - (self.DroppedAt or now) > 6 then
			MCB.PutBlock(self)
			return
		end

		if self:GetPos().z < -16000 then
			self:Remove()
			return
		end
	end

	self:NextThink(CurTime() + 0.1)
	return true
end

-- E on TNT lights it, like flint and steel
function ENT:Use(activator)
	if self:GetLoose() then return end
	if self:GetBlock() == "tnt" then
		MCB.QueueFlat("ignite", self:GetCellX(), self:GetCellY(), self:GetCellZ())
	end
end

function ENT:OnTakeDamage(dmg)
	if self:GetLoose() then return end
	local name = self:GetBlock()
	local t = dmg:GetDamageType()
	if name == "tnt" and bit.band(t, DMG_BURN + DMG_SLOWBURN) ~= 0 then
		MCB.QueueFlat("ignite", self:GetCellX(), self:GetCellY(), self:GetCellZ())
	elseif (name:find("glass", 1, true) or name:find("leaves", 1, true)) and bit.band(t, DMG_BULLET + DMG_BUCKSHOT + DMG_CLUB + DMG_SLASH) ~= 0 then
		-- bullets shatter glass and shred leaves; Minecraft removes the block and reports it
		MCB.QueueFlat("break", self:GetCellX(), self:GetCellY(), self:GetCellZ())
	end
end
