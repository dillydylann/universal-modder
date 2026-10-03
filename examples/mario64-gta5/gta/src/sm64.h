// libsm64, loaded at run time from Mario64GTA\sm64.dll (built from source by fetch_deps.sh; it isn't
// redistributed here). Loading it by hand means the ASI needs no import library from the MinGW build, and a
// missing DLL is a message in game rather than a game that won't start.
#pragma once
#include <libsm64.h>
#include <windows.h>

struct Sm64
{
#define SM64_FN(name) decltype(&::name) name = nullptr;
	SM64_FN(sm64_register_debug_print_function)
	SM64_FN(sm64_global_init)
	SM64_FN(sm64_global_terminate)
	SM64_FN(sm64_static_surfaces_load)
	SM64_FN(sm64_mario_create)
	SM64_FN(sm64_mario_tick)
	SM64_FN(sm64_mario_delete)
	SM64_FN(sm64_set_mario_position)
	SM64_FN(sm64_set_mario_faceangle)
	SM64_FN(sm64_set_mario_velocity)
	SM64_FN(sm64_set_mario_water_level)
	SM64_FN(sm64_set_mario_health)
	SM64_FN(sm64_mario_take_damage)
	SM64_FN(sm64_mario_attack)
	SM64_FN(sm64_surface_object_create)
	SM64_FN(sm64_surface_object_move)
	SM64_FN(sm64_surface_object_delete)
	SM64_FN(sm64_surface_find_floor_height)
#undef SM64_FN

	HMODULE module = nullptr;

	// true when every function was found
	bool load(const char *path)
	{
		module = LoadLibraryA(path);
		if (!module)
			return false;
		bool ok = true;
#define SM64_GET(name) ok &= (name = reinterpret_cast<decltype(name)>(reinterpret_cast<void *>(GetProcAddress(module, #name)))) != nullptr;
		SM64_GET(sm64_register_debug_print_function)
		SM64_GET(sm64_global_init)
		SM64_GET(sm64_global_terminate)
		SM64_GET(sm64_static_surfaces_load)
		SM64_GET(sm64_mario_create)
		SM64_GET(sm64_mario_tick)
		SM64_GET(sm64_mario_delete)
		SM64_GET(sm64_set_mario_position)
		SM64_GET(sm64_set_mario_faceangle)
		SM64_GET(sm64_set_mario_velocity)
		SM64_GET(sm64_set_mario_water_level)
		SM64_GET(sm64_set_mario_health)
		SM64_GET(sm64_mario_take_damage)
		SM64_GET(sm64_mario_attack)
		SM64_GET(sm64_surface_object_create)
		SM64_GET(sm64_surface_object_move)
		SM64_GET(sm64_surface_object_delete)
		SM64_GET(sm64_surface_find_floor_height)
#undef SM64_GET
		if (!ok)
			unload();
		return ok;
	}
	void unload()
	{
		if (module)
			FreeLibrary(module);
		module = nullptr;
	}
};
