-- Shared: settings, the GMod <-> Minecraft coordinate mapping, Minecraft's water, and swimming in it.
--
-- Mapping (1 block = MCB.Scale() Hammer units, 40 by default so a 72-unit GMod player is 1.8 blocks tall like Steve):
--   GMod (x, y, z) -> Minecraft (x / S, z / S + yOffset, -y / S)
--   Minecraft yaw = -GMod yaw - 90; pitch is the same (down is positive in both)
-- yOffset is picked when the link starts, so the GMod player's feet land on Minecraft y = 64 (any map fits the
-- world's -64..320 height range).

MCB = MCB or {}

MCB.cvScale = CreateConVar("mcbridge_scale", "40", FCVAR_REPLICATED + FCVAR_ARCHIVE, "Hammer units per Minecraft block (change before connecting)", 8, 128)
MCB.cvHp = CreateConVar("mcbridge_hp_scale", "5", FCVAR_REPLICATED + FCVAR_ARCHIVE, "GMod health per Minecraft health point (100 GMod HP = 20 Minecraft HP)", 0.1, 100)

function MCB.Scale()
	return MCB.cvScale:GetFloat()
end

function MCB.YOffset()
	return GetGlobalInt("mcb_yoff", 64)
end

--- GMod position -> Minecraft position (three numbers).
function MCB.ToMC(v)
	local s = MCB.Scale()
	return v.x / s, v.z / s + MCB.YOffset(), -v.y / s
end

--- Minecraft position -> GMod Vector.
function MCB.ToGM(x, y, z)
	local s = MCB.Scale()
	return Vector(x * s, -z * s, (y - MCB.YOffset()) * s)
end

--- The Minecraft block a GMod position is in.
function MCB.CellOf(v)
	local x, y, z = MCB.ToMC(v)
	return math.floor(x), math.floor(y), math.floor(z)
end

--- GMod position of a Minecraft block's centre.
function MCB.CellCenter(x, y, z)
	return MCB.ToGM(x + 0.5, y + 0.5, z + 0.5)
end

--- GMod velocity (units/s) -> Minecraft velocity (blocks/tick).
function MCB.VelToMC(v)
	local s = MCB.Scale() * 20
	return v.x / s, v.z / s, -v.y / s
end

function MCB.YawToMC(yaw)
	return math.NormalizeAngle(-yaw - 90)
end

function MCB.YawToGM(yaw)
	return math.NormalizeAngle(-yaw - 90)
end

--- One number per block (exact: |x|,|z| < 65536 and -512 <= y < 512 always hold for GMod maps).
function MCB.Key(x, y, z)
	return ((x + 65536) * 1024 + (y + 512)) * 131072 + (z + 65536)
end

function MCB.Unkey(k)
	local z = k % 131072
	k = (k - z) / 131072
	local y = k % 1024
	local x = (k - y) / 1024
	return x - 65536, y - 512, z - 65536
end

-- ---- Minecraft's water, mirrored on both realms: key -> level (1-8, 8 = a source) ----

MCB.Water = MCB.Water or {}

function MCB.WaterAt(v)
	return MCB.Water[MCB.Key(MCB.CellOf(v))]
end

if SERVER then
	util.AddNetworkString("mcb_water")
	util.AddNetworkString("mcb_hello")
end

--- Write water changes {key = level or 0}; the client applies them.
local function writeWater(changes)
	local n = table.Count(changes)
	net.WriteUInt(n, 16)
	for k, level in pairs(changes) do
		local x, y, z = MCB.Unkey(k)
		net.WriteInt(x, 20)
		net.WriteInt(y, 11)
		net.WriteInt(z, 20)
		net.WriteUInt(level, 4)
	end
end

if SERVER then
	--- Apply water changes here and send them to the clients (in chunks a net message can carry).
	function MCB.SetWater(changes, ply)
		local chunk, n = {}, 0
		local function send()
			if n == 0 then return end
			net.Start("mcb_water")
			writeWater(chunk)
			if ply then net.Send(ply) else net.Broadcast() end
			chunk, n = {}, 0
		end

		for k, level in pairs(changes) do
			if not ply then
				MCB.Water[k] = level > 0 and level or nil
			end

			chunk[k] = level
			n = n + 1
			if n >= 4000 then send() end
		end

		send()
	end

	net.Receive("mcb_hello", function(_, ply)
		MCB.SetWater(MCB.Water, ply)
	end)

	-- landing in Minecraft water doesn't hurt, as in Minecraft (and GMod's own water)
	hook.Add("GetFallDamage", "mcb_water", function(ply)
		if MCB.WaterAt(ply:GetPos() + Vector(0, 0, 4)) then return 0 end
	end)
else
	net.Receive("mcb_water", function()
		for _ = 1, net.ReadUInt(16) do
			local x, y, z = net.ReadInt(20), net.ReadInt(11), net.ReadInt(20)
			local level = net.ReadUInt(4)
			MCB.Water[MCB.Key(x, y, z)] = level > 0 and level or nil
		end

		MCB.WaterDirty = true
	end)

	hook.Add("InitPostEntity", "mcb_hello", function()
		net.Start("mcb_hello")
		net.SendToServer()
	end)
end

-- ---- swimming in Minecraft water: buoyancy, drag, Space to rise and Ctrl to sink (shared, so it's predicted) ----

local SWIM_SPEED = 130

hook.Add("Move", "mcb_swim", function(ply, mv)
	if ply:GetMoveType() ~= MOVETYPE_WALK or ply:WaterLevel() >= 2 then return end

	local pos = mv:GetOrigin()
	local height = ply:OBBMaxs().z
	local waist = MCB.WaterAt(pos + Vector(0, 0, height * 0.5))
	if not waist and not MCB.WaterAt(pos + Vector(0, 0, 4)) then return end

	local ft = FrameTime()
	local vel = mv:GetVelocity()
	if waist then
		-- nearly weightless, with water's drag
		vel.z = vel.z + GetConVarNumber("sv_gravity") * 0.9 * ft
		vel = vel * math.max(0, 1 - 2.5 * ft)

		local ang = mv:GetMoveAngles()
		local wish = ang:Forward() * mv:GetForwardSpeed() + ang:Right() * mv:GetSideSpeed()
		wish.z = 0
		if wish:LengthSqr() > 1 then
			wish:Normalize()
			wish = wish * SWIM_SPEED
			local t = math.min(1, 4 * ft)
			vel.x = Lerp(t, vel.x, wish.x)
			vel.y = Lerp(t, vel.y, wish.y)
		end

		if mv:KeyDown(IN_JUMP) then
			vel.z = math.max(vel.z, SWIM_SPEED * 0.8)
		elseif mv:KeyDown(IN_DUCK) then
			vel.z = math.min(vel.z, -SWIM_SPEED * 0.8)
		end
	else
		-- wading: slowed down
		vel.x = vel.x * math.max(0, 1 - 4 * ft)
		vel.y = vel.y * math.max(0, 1 - 4 * ft)
	end

	mv:SetVelocity(vel)
end)
