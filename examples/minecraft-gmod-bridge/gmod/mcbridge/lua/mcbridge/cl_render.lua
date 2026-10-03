-- Client: drawing Minecraft in GMod. Blocks are batched into a few meshes (rebuilt when they change), mobs are
-- drawn as box models with their Minecraft skins, and Minecraft's water as translucent cells.
--
-- Textures come from your own Minecraft install: tools/extract_textures.py copies them out of the client jar into
-- materials/mcbridge/. Without them, everything is drawn in flat colours.

local WHITE = CreateMaterial("mcb_white", "UnlitGeneric", {
	["$basetexture"] = "color/white", ["$vertexcolor"] = 1, ["$vertexalpha"] = 1, ["$nocull"] = 1,
})

local mats = {}

--- An unlit, vertex-coloured, alpha-tested material for a texture extracted from Minecraft (nil when missing).
function MCB.Mat(path, translucent)
	local id = path .. (translucent and "#t" or "")
	if mats[id] ~= nil then return mats[id] or nil end

	mats[id] = false
	if not file.Exists("materials/" .. path, "GAME") then return nil end

	local png = Material(path, "noclamp") -- no "smooth": point sampling, like Minecraft
	if png:IsError() then return nil end

	local m = CreateMaterial("mcb_" .. util.CRC(id), "UnlitGeneric", {
		["$basetexture"] = "color/white",
		["$vertexcolor"] = 1,
		["$vertexalpha"] = 1,
		["$nocull"] = 1,
		["$alphatest"] = translucent and 0 or 1,
		["$translucent"] = translucent and 1 or 0,
	})
	m:SetTexture("$basetexture", png:GetTexture("$basetexture"))
	mats[id] = m
	return m
end

-- ---- box faces ----

-- corners of each face of an axis-aligned box (lo, hi), top-left first as seen from outside, in a frame where +x is
-- the front, +y the left and +z up
local FACES = {
	front = function(lo, hi) return Vector(hi.x, lo.y, hi.z), Vector(hi.x, hi.y, hi.z), Vector(hi.x, hi.y, lo.z), Vector(hi.x, lo.y, lo.z) end,
	back = function(lo, hi) return Vector(lo.x, hi.y, hi.z), Vector(lo.x, lo.y, hi.z), Vector(lo.x, lo.y, lo.z), Vector(lo.x, hi.y, lo.z) end,
	right = function(lo, hi) return Vector(lo.x, lo.y, hi.z), Vector(hi.x, lo.y, hi.z), Vector(hi.x, lo.y, lo.z), Vector(lo.x, lo.y, lo.z) end,
	left = function(lo, hi) return Vector(hi.x, hi.y, hi.z), Vector(lo.x, hi.y, hi.z), Vector(lo.x, hi.y, lo.z), Vector(hi.x, hi.y, lo.z) end,
	top = function(lo, hi) return Vector(lo.x, lo.y, hi.z), Vector(lo.x, hi.y, hi.z), Vector(hi.x, hi.y, hi.z), Vector(hi.x, lo.y, hi.z) end,
	bottom = function(lo, hi) return Vector(hi.x, lo.y, lo.z), Vector(hi.x, hi.y, lo.z), Vector(lo.x, hi.y, lo.z), Vector(lo.x, lo.y, lo.z) end,
}
local FACE_ORDER = { "front", "back", "right", "left", "top", "bottom" }
-- Minecraft-style fixed shading per face
local SHADE = { top = 1, bottom = 0.5, front = 0.8, back = 0.8, right = 0.65, left = 0.65 }
-- the neighbouring block (Minecraft dx, dy, dz) for a block face in GMod's frame (+x east, +y north = -z, +z up)
local NEIGHBOUR = {
	front = { 1, 0, 0 }, back = { -1, 0, 0 }, left = { 0, 0, -1 }, right = { 0, 0, 1 }, top = { 0, 1, 0 }, bottom = { 0, -1, 0 },
}

local function quad(a, b, c, d, u0, v0, u1, v1, r, g, bl, al)
	mesh.Position(a) mesh.TexCoord(0, u0, v0) mesh.Color(r, g, bl, al) mesh.AdvanceVertex()
	mesh.Position(b) mesh.TexCoord(0, u1, v0) mesh.Color(r, g, bl, al) mesh.AdvanceVertex()
	mesh.Position(c) mesh.TexCoord(0, u1, v1) mesh.Color(r, g, bl, al) mesh.AdvanceVertex()
	mesh.Position(d) mesh.TexCoord(0, u0, v1) mesh.Color(r, g, bl, al) mesh.AdvanceVertex()
end

-- ---- block textures ----

local TINTED = { grass_block_top = true, oak_leaves = true, birch_leaves = true, jungle_leaves = true, acacia_leaves = true,
	dark_oak_leaves = true, mangrove_leaves = true, vine = true, short_grass = true, tall_grass = true, fern = true }
local BOTTOM = { grass_block = "dirt", podzol = "dirt", mycelium = "dirt", dirt_path = "dirt" }

local function blockTex(name)
	return "mcbridge/block/" .. name .. ".png"
end

local function has(name)
	return file.Exists("materials/" .. blockTex(name), "GAME")
end

local faceCache = {}

--- Material and tint for one side ("top", "bottom" or "side") of a block.
local function faceMat(name, side)
	local id = name .. "/" .. side
	local c = faceCache[id]
	if c then return c[1], c[2] end

	local tex
	if side == "top" then
		tex = has(name .. "_top") and name .. "_top" or name
	elseif side == "bottom" then
		tex = BOTTOM[name] or (has(name .. "_bottom") and name .. "_bottom") or (has(name .. "_top") and name .. "_top") or name
	else
		tex = has(name .. "_side") and name .. "_side" or name
	end

	local mat = MCB.Mat(blockTex(tex))
	local tint
	if not mat then
		-- no texture: a flat colour that stays the same for each block type
		local h = tonumber(util.CRC(name)) or 0
		tint = Color(90 + h % 140, 90 + math.floor(h / 140) % 140, 90 + math.floor(h / 19600) % 140)
		mat = WHITE
	elseif TINTED[tex] or name:find("leaves", 1, true) then
		tint = Color(124, 189, 107)
	else
		tint = Color(255, 255, 255)
	end

	faceCache[id] = { mat, tint }
	return mat, tint
end

local function sideOf(face)
	return (face == "top" or face == "bottom") and face or "side"
end

local function occludes(name)
	return name ~= nil and not name:find("glass", 1, true) and not name:find("leaves", 1, true)
end

local function light(pos, normal)
	local c = render.ComputeLighting(pos, normal)
	local l = (c.x + c.y + c.z) / 3
	return math.Clamp(l ^ (1 / 2.2) * 1.2, 0.2, 1)
end

--- Draw a single block (a loose one the physgun has, or lit TNT) around an entity, `size` blocks wide.
function MCB.DrawBlockAt(ent, name, size, flash)
	local h = MCB.Scale() * size * 0.5
	local lo, hi = Vector(-h, -h, -h), Vector(h, h, h)
	local center = ent:WorldSpaceCenter()
	local l = light(center, Vector(0, 0, 1))
	for _, face in ipairs(FACE_ORDER) do
		local mat, tint = faceMat(name, sideOf(face))
		local a, b, c, d = FACES[face](lo, hi)
		local s = SHADE[face] * l * 255
		local k = flash and 1.6 or 1
		render.SetMaterial(mat)
		mesh.Begin(MATERIAL_QUADS, 1)
		quad(ent:LocalToWorld(a), ent:LocalToWorld(b), ent:LocalToWorld(c), ent:LocalToWorld(d), 0, 0, 1, 1,
			math.min(255, tint.r * s / 255 * k), math.min(255, tint.g * s / 255 * k), math.min(255, tint.b * s / 255 * k), 255)
		mesh.End()
	end
end

-- ---- static blocks: batched meshes ----

local static = {} -- key -> name, every static mc_block
local batches = {} -- { mat, IMesh }
local MAX_TRIS = 10000

timer.Create("mcb_static", 0.25, 0, function()
	local seen, dirty = {}, false
	for _, e in ipairs(ents.FindByClass("mc_block")) do
		local name = e:GetBlock()
		if not e:GetLoose() and name ~= "" then
			local k = MCB.Key(e:GetCellX(), e:GetCellY(), e:GetCellZ())
			seen[k] = true
			if static[k] ~= name then
				static[k] = name
				dirty = true
			end
		end
	end

	for k in pairs(static) do
		if not seen[k] then
			static[k] = nil
			dirty = true
		end
	end

	if dirty then MCB.BlocksDirty = true end
end)

local function rebuildBlocks()
	MCB.BlocksDirty = false
	for _, b in ipairs(batches) do b[2]:Destroy() end
	batches = {}

	local s = MCB.Scale()
	local byMat = {}
	for k, name in pairs(static) do
		local x, y, z = MCB.Unkey(k)
		local center = MCB.CellCenter(x, y, z)
		local lo, hi = center - Vector(s, s, s) * 0.5, center + Vector(s, s, s) * 0.5
		for _, face in ipairs(FACE_ORDER) do
			local n = NEIGHBOUR[face]
			if not occludes(static[MCB.Key(x + n[1], y + n[2], z + n[3])]) then
				local mat, tint = faceMat(name, sideOf(face))
				local a, b, c, d = FACES[face](lo, hi)
				local normal = (a - c):Cross(b - d):GetNormalized()
				local l = light(center + normal * s * 0.55, normal) * SHADE[face]
				local col = Color(tint.r * l, tint.g * l, tint.b * l, 255)
				local list = byMat[mat]
				if not list then
					list = {}
					byMat[mat] = list
				end

				-- two triangles; the mesh is drawn with $nocull, so winding doesn't matter
				list[#list + 1] = { pos = a, u = 0, v = 0, color = col, normal = normal }
				list[#list + 1] = { pos = b, u = 1, v = 0, color = col, normal = normal }
				list[#list + 1] = { pos = c, u = 1, v = 1, color = col, normal = normal }
				list[#list + 1] = { pos = a, u = 0, v = 0, color = col, normal = normal }
				list[#list + 1] = { pos = c, u = 1, v = 1, color = col, normal = normal }
				list[#list + 1] = { pos = d, u = 0, v = 1, color = col, normal = normal }
			end
		end
	end

	for mat, verts in pairs(byMat) do
		for first = 1, #verts, MAX_TRIS * 3 do
			local part = {}
			for i = first, math.min(#verts, first + MAX_TRIS * 3 - 1) do part[#part + 1] = verts[i] end
			local m = Mesh(mat)
			m:BuildFromTriangles(part)
			batches[#batches + 1] = { mat, m }
		end
	end
end

hook.Add("PostDrawOpaqueRenderables", "mcb_blocks", function(_, skybox, skybox3d)
	if skybox or skybox3d then return end
	if MCB.BlocksDirty then rebuildBlocks() end
	for _, b in ipairs(batches) do
		render.SetMaterial(b[1])
		b[2]:Draw()
	end
end)

-- ---- water ----

local waterMesh, waterMat
local WATER_COLOR = Color(63, 118, 228, 170)

local function rebuildWater()
	MCB.WaterDirty = false
	if waterMesh then waterMesh:Destroy() end
	waterMesh = nil
	waterMat = waterMat or MCB.Mat(blockTex("water_still"), true)
	if not waterMat then
		waterMat = CreateMaterial("mcb_water", "UnlitGeneric", {
			["$basetexture"] = "color/white", ["$vertexcolor"] = 1, ["$vertexalpha"] = 1, ["$translucent"] = 1, ["$nocull"] = 1,
		})
	end

	local s = MCB.Scale()
	local verts = {}
	-- water_still.png is an animation strip: the first 16x16 frame only
	local v1 = 1 / 32
	for k, level in pairs(MCB.Water) do
		local x, y, z = MCB.Unkey(k)
		local center = MCB.CellCenter(x, y, z)
		local above = MCB.Water[MCB.Key(x, y + 1, z)]
		local height = above and 1 or (level >= 8 and 8 / 9 or level / 9)
		local lo = center - Vector(s, s, s) * 0.5
		local hi = lo + Vector(s, s, s * height)
		for _, face in ipairs(FACE_ORDER) do
			local n = NEIGHBOUR[face]
			local nk = MCB.Key(x + n[1], y + n[2], z + n[3])
			if not MCB.Water[nk] and not occludes(static[nk]) then
				local a, b, c, d = FACES[face](lo, hi)
				for _, vtx in ipairs({ { a, 0, 0 }, { b, 1, 0 }, { c, 1, v1 }, { a, 0, 0 }, { c, 1, v1 }, { d, 0, v1 } }) do
					verts[#verts + 1] = { pos = vtx[1], u = vtx[2], v = vtx[3], color = WATER_COLOR }
				end
			end
		end

		if #verts >= MAX_TRIS * 3 then break end
	end

	if #verts > 0 then
		waterMesh = Mesh(waterMat)
		waterMesh:BuildFromTriangles(verts)
	end
end

hook.Add("PostDrawTranslucentRenderables", "mcb_water", function(_, skybox, skybox3d)
	if skybox or skybox3d then return end
	if MCB.WaterDirty or MCB.BlocksDirty then rebuildWater() end
	if waterMesh then
		render.SetMaterial(waterMat)
		waterMesh:Draw()
	end
end)

-- a blue tint while the camera is under Minecraft water
hook.Add("RenderScreenspaceEffects", "mcb_water", function()
	if MCB.WaterAt(EyePos()) then
		DrawColorModify({
			["$pp_colour_addr"] = 0, ["$pp_colour_addg"] = 0.02, ["$pp_colour_addb"] = 0.12,
			["$pp_colour_brightness"] = -0.05, ["$pp_colour_contrast"] = 0.9, ["$pp_colour_colour"] = 0.8,
			["$pp_colour_mulr"] = 0, ["$pp_colour_mulg"] = 0, ["$pp_colour_mulb"] = 0,
		})
	end
end)

-- ---- mobs ----

-- Box models in Minecraft model pixels (16 per block), +x forward, +y left, +z up, feet at 0. Each part: pivot,
-- box relative to the pivot, the box's texture offset (u, v) and size along y (w), z (h), x (d), and a swing:
-- "arms" (held forward, like a zombie's), "leg" or "leg2" (walking, opposite phases).
local function part(pivot, lo, hi, u, v, w, h, d, swing)
	return { pivot = pivot, lo = lo, hi = hi, u = u, v = v, w = w, h = h, d = d, swing = swing }
end

local HUMANOID = {
	height = 32,
	part(Vector(0, 0, 24), Vector(-4, -4, 0), Vector(4, 4, 8), 0, 0, 8, 8, 8),
	part(Vector(0, 0, 12), Vector(-2, -4, 0), Vector(2, 4, 12), 16, 16, 8, 12, 4),
	part(Vector(0, -6, 22), Vector(-2, -2, -10), Vector(2, 2, 2), 40, 16, 4, 12, 4, "arms"),
	part(Vector(0, 6, 22), Vector(-2, -2, -10), Vector(2, 2, 2), 40, 16, 4, 12, 4, "arms"),
	part(Vector(0, -2, 12), Vector(-2, -2, -12), Vector(2, 2, 0), 0, 16, 4, 12, 4, "leg"),
	part(Vector(0, 2, 12), Vector(-2, -2, -12), Vector(2, 2, 0), 0, 16, 4, 12, 4, "leg2"),
}

local SKELETON = {
	height = 32,
	part(Vector(0, 0, 24), Vector(-4, -4, 0), Vector(4, 4, 8), 0, 0, 8, 8, 8),
	part(Vector(0, 0, 12), Vector(-2, -4, 0), Vector(2, 4, 12), 16, 16, 8, 12, 4),
	part(Vector(0, -5, 22), Vector(-1, -1, -10), Vector(1, 1, 2), 40, 16, 2, 12, 2, "arms"),
	part(Vector(0, 5, 22), Vector(-1, -1, -10), Vector(1, 1, 2), 40, 16, 2, 12, 2, "arms"),
	part(Vector(0, -2, 12), Vector(-1, -1, -12), Vector(1, 1, 0), 0, 16, 2, 12, 2, "leg"),
	part(Vector(0, 2, 12), Vector(-1, -1, -12), Vector(1, 1, 0), 0, 16, 2, 12, 2, "leg2"),
}

local CREEPER = {
	height = 26,
	part(Vector(0, 0, 18), Vector(-4, -4, 0), Vector(4, 4, 8), 0, 0, 8, 8, 8),
	part(Vector(0, 0, 6), Vector(-2, -4, 0), Vector(2, 4, 12), 16, 16, 8, 12, 4),
	part(Vector(4, -2, 6), Vector(-2, -2, -6), Vector(2, 2, 0), 0, 16, 4, 6, 4, "leg"),
	part(Vector(4, 2, 6), Vector(-2, -2, -6), Vector(2, 2, 0), 0, 16, 4, 6, 4, "leg2"),
	part(Vector(-4, -2, 6), Vector(-2, -2, -6), Vector(2, 2, 0), 0, 16, 4, 6, 4, "leg2"),
	part(Vector(-4, 2, 6), Vector(-2, -2, -6), Vector(2, 2, 0), 0, 16, 4, 6, 4, "leg"),
}

local MODELS = {
	zombie = { HUMANOID, "zombie" }, husk = { HUMANOID, "husk" }, drowned = { HUMANOID, "drowned" },
	skeleton = { SKELETON, "skeleton" }, stray = { SKELETON, "stray" }, wither_skeleton = { SKELETON, "wither_skeleton" },
	bogged = { SKELETON, "bogged" },
	creeper = { CREEPER, "creeper" },
}

-- flat colours for everything else (and when the skins weren't extracted)
local COLORS = {
	zombie = Color(70, 120, 60), husk = Color(140, 120, 80), drowned = Color(60, 130, 130), skeleton = Color(200, 200, 200),
	stray = Color(160, 180, 180), wither_skeleton = Color(40, 40, 40), creeper = Color(80, 170, 70), spider = Color(60, 50, 45),
	cave_spider = Color(30, 70, 80), enderman = Color(20, 20, 25), slime = Color(110, 190, 90), magma_cube = Color(120, 40, 20),
	witch = Color(90, 60, 110), cow = Color(90, 60, 40), pig = Color(240, 160, 160), sheep = Color(235, 235, 235),
	chicken = Color(245, 245, 245), wolf = Color(200, 200, 200), villager = Color(150, 110, 80), iron_golem = Color(200, 195, 185),
	pillager = Color(90, 90, 90), vindicator = Color(80, 90, 90), blaze = Color(240, 180, 40),
}

local function emitBox(ent, scale, p, rot, tex, tw, th, r, g, b)
	for _, face in ipairs(FACE_ORDER) do
		local u, v, fw, fh
		if face == "top" then u, v, fw, fh = p.u + p.d, p.v, p.w, p.d
		elseif face == "bottom" then u, v, fw, fh = p.u + p.d + p.w, p.v, p.w, p.d
		elseif face == "right" then u, v, fw, fh = p.u, p.v + p.d, p.d, p.h
		elseif face == "front" then u, v, fw, fh = p.u + p.d, p.v + p.d, p.w, p.h
		elseif face == "left" then u, v, fw, fh = p.u + p.d + p.w, p.v + p.d, p.d, p.h
		else u, v, fw, fh = p.u + p.d + p.w + p.d, p.v + p.d, p.w, p.h end

		local c = { FACES[face](p.lo, p.hi) }
		for i = 1, 4 do
			local q = Vector(c[i])
			if rot then q:Rotate(rot) end
			c[i] = ent:LocalToWorld((p.pivot + q) * scale)
		end

		local s = SHADE[face]
		if tex then
			quad(c[1], c[2], c[3], c[4], u / tw, v / th, (u + fw) / tw, (v + fh) / th, r * s, g * s, b * s, 255)
		else
			quad(c[1], c[2], c[3], c[4], 0, 0, 1, 1, r * s, g * s, b * s, 255)
		end
	end
end

function MCB.DrawMob(ent)
	local kind = ent:GetKind()
	local s = MCB.Scale()
	local hurt = CurTime() - ent:GetHurtTime() < 0.35

	if kind == "tnt" then
		-- Minecraft's lit TNT flashes white twice a second
		MCB.DrawBlockAt(ent, "tnt", 0.98, CurTime() % 0.5 < 0.25)
		return
	end

	-- walking: how fast it moved since the last frame
	local pos = ent:GetPos()
	local last = ent.mcbLast or pos
	local speed = FrameTime() > 0 and (pos - last):Length2D() / FrameTime() or 0
	ent.mcbLast = pos
	ent.mcbSpeed = Lerp(math.min(1, FrameTime() * 8), ent.mcbSpeed or 0, speed)
	local swing = math.sin(CurTime() * 9) * 35 * math.min(1, ent.mcbSpeed / (s * 2))

	local l = light(ent:WorldSpaceCenter(), Vector(0, 0, 1)) * 255
	local r, g, b = l, l, l
	if hurt then g, b = l * 0.35, l * 0.35 end

	local model = MODELS[kind]
	local mat = model and MCB.Mat("mcbridge/entity/" .. model[2] .. ".png")
	if model and mat then
		local parts = model[1]
		local scale = ent:GetH() * s / parts.height
		local tex = mat:GetTexture("$basetexture")
		local tw, th = tex:Width(), tex:Height()
		render.SetMaterial(mat)
		mesh.Begin(MATERIAL_QUADS, #parts * 6)
		for _, p in ipairs(parts) do
			local rot
			if p.swing == "arms" then rot = Angle(-90 + swing * 0.15, 0, 0)
			elseif p.swing == "leg" then rot = Angle(swing, 0, 0)
			elseif p.swing == "leg2" then rot = Angle(-swing, 0, 0) end
			emitBox(ent, scale, p, rot, true, tw, th, r, g, b)
		end
		mesh.End()
		return
	end

	-- anything else: a box of its size in its colour
	local col = COLORS[kind] or Color(150, 150, 150)
	local hw, hh = ent:GetW() * s * 0.5, ent:GetH() * s
	render.SetMaterial(WHITE)
	mesh.Begin(MATERIAL_QUADS, 6)
	emitBox(ent, 1, { pivot = Vector(0, 0, 0), lo = Vector(-hw, -hw, 0), hi = Vector(hw, hw, hh), u = 0, v = 0, w = 0, h = 0, d = 0 }, nil, false, 1, 1,
		col.r * r / 255, col.g * g / 255, col.b * b / 255)
	mesh.End()
end
