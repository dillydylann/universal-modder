-- Minecraft Bridge: real Minecraft (with the gmodbridge Fabric mod) running next to Garry's Mod.
-- Loads the shared core, the server link and the client renderer.

MCB = MCB or {}

AddCSLuaFile("mcbridge/sh_core.lua")
AddCSLuaFile("mcbridge/cl_render.lua")
include("mcbridge/sh_core.lua")

if SERVER then
	include("mcbridge/sv_link.lua")
	include("mcbridge/sv_world.lua")
	include("mcbridge/sv_mobs.lua")
else
	include("mcbridge/cl_render.lua")
end
