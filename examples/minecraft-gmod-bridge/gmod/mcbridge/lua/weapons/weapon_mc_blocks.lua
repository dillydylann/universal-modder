-- The Minecraft tool: build and break Minecraft blocks, light TNT and spawn Minecraft mobs from inside GMod.
-- Left click places (or spawns), right click breaks, reload picks the next item.
AddCSLuaFile()

SWEP.PrintName = "Minecraft blocks"
SWEP.Author = "universal-modder"
SWEP.Category = "Minecraft"
SWEP.Instructions = "Left click: place / spawn. Right click: break. Reload: next item. E on TNT also lights it."
SWEP.Spawnable = true
SWEP.AdminOnly = false
SWEP.Slot = 5
SWEP.SlotPos = 3
SWEP.ViewModel = "models/weapons/c_toolgun.mdl"
SWEP.WorldModel = "models/weapons/w_toolgun.mdl"
SWEP.UseHands = true
SWEP.DrawAmmo = false

SWEP.Primary.ClipSize = -1
SWEP.Primary.DefaultClip = -1
SWEP.Primary.Automatic = false
SWEP.Primary.Ammo = "none"
SWEP.Secondary.ClipSize = -1
SWEP.Secondary.DefaultClip = -1
SWEP.Secondary.Automatic = false
SWEP.Secondary.Ammo = "none"

-- { label, kind, value }: "block" places a block, "fire" lights TNT, "mob" spawns a mob
SWEP.Palette = {
	{ "Stone", "block", "stone" }, { "Dirt", "block", "dirt" }, { "Grass block", "block", "grass_block" },
	{ "Oak planks", "block", "oak_planks" }, { "Cobblestone", "block", "cobblestone" }, { "Glass", "block", "glass" },
	{ "Sand", "block", "sand" }, { "TNT", "block", "tnt" }, { "Water", "block", "water" },
	{ "Flint and steel", "fire", "" },
	{ "Zombie", "mob", "zombie" }, { "Skeleton", "mob", "skeleton" }, { "Creeper", "mob", "creeper" },
	{ "Spider", "mob", "spider" }, { "Pig", "mob", "pig" }, { "Cow", "mob", "cow" },
}

function SWEP:SetupDataTables()
	self:NetworkVar("Int", 0, "Choice")
end

function SWEP:Initialize()
	self:SetHoldType("pistol")
end

function SWEP:Item()
	return self.Palette[self:GetChoice() + 1] or self.Palette[1]
end

local REACH = 400

function SWEP:Trace()
	local owner = self:GetOwner()
	return util.TraceLine({
		start = owner:GetShootPos(),
		endpos = owner:GetShootPos() + owner:GetAimVector() * REACH,
		filter = owner,
		mask = MASK_SOLID,
	})
end

function SWEP:PrimaryAttack()
	self:SetNextPrimaryFire(CurTime() + 0.2)
	local tr = self:Trace()
	if not tr.Hit then return end

	self:EmitSound("weapons/airboat/airboat_gun_lastshot1.wav", 60, 140)
	self:SendWeaponAnim(ACT_VM_PRIMARYATTACK)
	self:GetOwner():SetAnimation(PLAYER_ATTACK1)
	if CLIENT then return end

	local item = self:Item()
	local s = MCB.Scale()
	if item[2] == "fire" then
		if IsValid(tr.Entity) and tr.Entity:GetClass() == "mc_block" and tr.Entity:GetBlock() == "tnt" then
			tr.Entity:Use(self:GetOwner(), self:GetOwner(), USE_ON, 1)
		end
	elseif item[2] == "mob" then
		local x, y, z = MCB.ToMC(tr.HitPos + tr.HitNormal * 4)
		MCB.Queue("spawn", { item[3], x, y, z })
	else
		-- the cell in front of the face that was hit
		local x, y, z = MCB.CellOf(tr.HitPos + tr.HitNormal * s * 0.5)
		MCB.Queue("put", { x, y, z, item[3] })
	end
end

function SWEP:SecondaryAttack()
	self:SetNextSecondaryFire(CurTime() + 0.2)
	local tr = self:Trace()
	if not IsValid(tr.Entity) or tr.Entity:GetClass() ~= "mc_block" then return end

	self:EmitSound("physics/concrete/concrete_break2.wav", 60, 120)
	self:SendWeaponAnim(ACT_VM_PRIMARYATTACK)
	if CLIENT or tr.Entity:GetLoose() then return end
	MCB.QueueFlat("break", tr.Entity:GetCellX(), tr.Entity:GetCellY(), tr.Entity:GetCellZ())
end

function SWEP:Reload()
	if (self.NextCycle or 0) > CurTime() then return end
	self.NextCycle = CurTime() + 0.25
	if SERVER then
		self:SetChoice((self:GetChoice() + 1) % #self.Palette)
	end
end

if CLIENT then
	function SWEP:DrawHUD()
		local item = self:Item()
		draw.SimpleTextOutlined(item[1], "DermaLarge", ScrW() / 2, ScrH() * 0.62, color_white, TEXT_ALIGN_CENTER,
			TEXT_ALIGN_CENTER, 2, color_black)
		draw.SimpleTextOutlined("R: next item", "DermaDefault", ScrW() / 2, ScrH() * 0.62 + 24, color_white,
			TEXT_ALIGN_CENTER, TEXT_ALIGN_CENTER, 1, color_black)

		-- outline the cell a block would go into
		if item[2] ~= "block" then return end
		local tr = self:Trace()
		if not tr.Hit then return end
		local s = MCB.Scale()
		local center = MCB.CellCenter(MCB.CellOf(tr.HitPos + tr.HitNormal * s * 0.5))
		cam.Start3D()
		render.DrawWireframeBox(center, Angle(0, 0, 0), Vector(-s, -s, -s) * 0.5, Vector(s, s, s) * 0.5, color_white, true)
		cam.End3D()
	end
end
