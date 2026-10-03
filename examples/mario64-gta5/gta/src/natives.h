// The GTA V natives the Mario script calls, by hash. Names, hashes and argument lists checked against alloc8or's
// native DB (gta5-nativedb-data); ScriptHookV translates these original hashes for the running game build.
#pragma once
#include <types.h>
#include <nativeCaller.h>

namespace natives
{
	// player, peds, entities
	inline Player PlayerId() { return invoke<Player>(0x4F8644AF03D0E0D6); }
	inline Ped PlayerPedId() { return invoke<Ped>(0xD80958FC74E988A6); }
	inline BOOL DoesEntityExist(Entity e) { return invoke<BOOL>(0x7239B21A38F536BA, e); }
	inline Vector3 GetEntityCoords(Entity e) { return invoke<Vector3>(0x3FEF770D40960D5A, e, TRUE); }
	inline Vector3 GetEntityRotation(Entity e) { return invoke<Vector3>(0xAFBD61CC738D9EB9, e, 2); }
	inline float GetEntityHeading(Entity e) { return invoke<float>(0xE83D4F9BA2A38914, e); }
	inline Hash GetEntityModel(Entity e) { return invoke<Hash>(0x9F47B058362C84B5, e); }
	inline void GetModelDimensions(Hash m, Vector3 *mn, Vector3 *mx) { invoke<Void>(0x03E8D3D5F549087A, m, mn, mx); }
	inline void SetEntityCoordsNoOffset(Entity e, float x, float y, float z) { invoke<Void>(0x239A3351AC1DA385, e, x, y, z, FALSE, FALSE, FALSE); }
	inline void SetEntityHeading(Entity e, float h) { invoke<Void>(0x8E2530AA8ADA980E, e, h); }
	inline void SetEntityVisible(Entity e, BOOL visible) { invoke<Void>(0xEA1C610A04DB6BBB, e, visible, FALSE); }
	inline void FreezeEntityPosition(Entity e, BOOL t) { invoke<Void>(0x428CA6DBD1094446, e, t); }
	inline void SetEntityVelocity(Entity e, float x, float y, float z) { invoke<Void>(0x1C99BB7B6E96D16F, e, x, y, z); }
	inline int GetEntityHealth(Entity e) { return invoke<int>(0xEEF059FAD016D209, e); }
	inline void SetEntityHealth(Entity e, int health) { invoke<Void>(0x6B76DC1F3AE6E6A3, e, health, 0, 0); }
	inline int GetEntityMaxHealth(Entity e) { return invoke<int>(0x15D757606D170C3C, e); }
	inline void SetEntityMaxHealth(Entity e, int v) { invoke<Void>(0x166E7CF68597D8B5, e, v); }
	inline void ClearEntityLastDamageEntity(Entity e) { invoke<Void>(0xA72CD9CA74A5ECBA, e); }
	inline BOOL IsPedDeadOrDying(Ped p) { return invoke<BOOL>(0x3317DEDB88C95038, p, TRUE); }
	inline BOOL IsPedHuman(Ped p) { return invoke<BOOL>(0xB980061DA992779D, p); }
	inline BOOL IsPedAPlayer(Ped p) { return invoke<BOOL>(0x12534C348C6CB68B, p); }
	inline BOOL IsPedShooting(Ped p) { return invoke<BOOL>(0x34616828CD07F1A1, p); }
	inline BOOL IsPedInMeleeCombat(Ped p) { return invoke<BOOL>(0x4E209B2C1EAD5159, p); }
	inline BOOL IsPedInAnyVehicle(Ped p) { return invoke<BOOL>(0x997ABD671D25CA0B, p, FALSE); }
	inline int GetPedType(Ped p) { return invoke<int>(0xFF059E1E4C01E63C, p); }
	inline void SetPedCanRagdoll(Ped p, BOOL t) { invoke<Void>(0xB128377056A54E2A, p, t); }
	inline void SetPedMaxHealth(Ped p, int v) { invoke<Void>(0xF5F6378C4F3419D3, p, v); }
	inline void SetPedSuffersCriticalHits(Ped p, BOOL t) { invoke<Void>(0xEBD76F2359F190AC, p, t); }
	inline void SetPedDiesInWater(Ped p, BOOL t) { invoke<Void>(0x56CEF0AC79073BDE, p, t); }
	inline void SetCurrentPedWeapon(Ped p, Hash w, BOOL inHand) { invoke<Void>(0xADF692B254977C0C, p, w, inHand); }
	inline void SetPedArmour(Ped p, int amount) { invoke<Void>(0xCEA04D83135264CC, p, amount); }
	inline void SetPedToRagdoll(Ped p, int ms) { invoke<BOOL>(0xAE99FB955581844A, p, ms, ms, 0, FALSE, FALSE, FALSE); }
	inline void ApplyDamageToPed(Ped p, int damage) { invoke<Void>(0x697157CED63F18D4, p, damage, FALSE, 0, 0); }
	inline void ApplyForceToEntity(Entity e, float x, float y, float z)
	{
		// force type 1 (an impulse), world direction, at the entity's centre
		invoke<Void>(0xC5F68BE9613E2D18, e, 1, x, y, z, 0.0f, 0.0f, 0.0f, 0, FALSE, TRUE, TRUE, FALSE, TRUE);
	}
	inline void GiveWeaponToPed(Ped p, Hash weapon, int ammo) { invoke<Void>(0xBF0FD6E56C964FCB, p, weapon, ammo, FALSE, TRUE); }
	inline void TaskCombatPed(Ped p, Ped target) { invoke<Void>(0xF166E48407BAC484, p, target, 0, 16); }
	inline void SetPedKeepTask(Ped p, BOOL on) { invoke<Void>(0x971D38760FBC02EF, p, on); }
	inline void SetPedCombatAttributes(Ped p, int attr, BOOL on) { invoke<Void>(0x9F7794730795E019, p, attr, on); }
	inline void SetPedAccuracy(Ped p, int accuracy) { invoke<Void>(0x7AEFB85C1D49DEB6, p, accuracy); }
	inline Hash GetHashKey(const char *s) { return invoke<Hash>(0xD24D37CC275948CC, s); }
	inline void RequestModel(Hash m) { invoke<Void>(0x963D27A58DF860AC, m); }
	inline BOOL HasModelLoaded(Hash m) { return invoke<BOOL>(0x98A4EB5D89A0C952, m); }
	inline void SetModelAsNoLongerNeeded(Hash m) { invoke<Void>(0xE532F5D78798DAAB, m); }
	inline Ped CreatePed(Hash model, float x, float y, float z, float h) { return invoke<Ped>(0xD49F9B0955C367DE, 4, model, x, y, z, h, FALSE, TRUE); }
	inline void SetPedAsNoLongerNeeded(Ped *p) { invoke<Void>(0x2595DD4236549CE3, p); }

	// wanted level
	inline int GetPlayerWantedLevel(Player p) { return invoke<int>(0xE28E54788CE8F12D, p); }
	inline void SetPlayerWantedLevel(Player p, int level) { invoke<Void>(0x39FF19C64EF7DA5B, p, level, FALSE); }
	inline void SetPlayerWantedLevelNow(Player p) { invoke<Void>(0xE0A7D1E497FFCD6F, p, FALSE); }

	// world queries
	inline int ShapeTestLosProbeNow(float x1, float y1, float z1, float x2, float y2, float z2, int flags, Entity ignore)
	{
		// START_EXPENSIVE_SYNCHRONOUS_SHAPE_TEST_LOS_PROBE: the result is ready straight away
		return invoke<int>(0x377906D8A31E5586, x1, y1, z1, x2, y2, z2, flags, ignore, 7);
	}
	inline int GetShapeTestResult(int handle, BOOL *hit, Vector3 *end, Vector3 *normal, Entity *entity)
	{
		return invoke<int>(0x3D87450E15D98694, handle, hit, end, normal, entity);
	}
	inline BOOL GetWaterHeight(float x, float y, float z, float *height) { return invoke<BOOL>(0xF6829842C06AE524, x, y, z, height); }

	// input
	inline void DisableAllControlActions(int group) { invoke<Void>(0x5F4B6931816E599B, group); }
	inline void EnableControlAction(int group, int control) { invoke<Void>(0x351220255D64C155, group, control, TRUE); }
	inline BOOL IsDisabledControlPressed(int group, int control) { return invoke<BOOL>(0xE2587F8CBBD87B1D, group, control); }
	inline BOOL IsDisabledControlJustPressed(int group, int control) { return invoke<BOOL>(0x91AEF906BCA88877, group, control); }
	inline BOOL IsUsingKeyboardAndMouse() { return invoke<BOOL>(0xA571D46727E2B718, 0); }
	inline float GetDisabledControlNormal(int group, int control) { return invoke<float>(0x11E65974A982637C, group, control); }

	// camera
	inline Cam CreateCam(const char *name) { return invoke<Cam>(0xC3981DCE61D9E13F, name, TRUE); }
	inline void DestroyCam(Cam c) { invoke<Void>(0x865908C81A2C22E9, c, FALSE); }
	inline void SetCamCoord(Cam c, float x, float y, float z) { invoke<Void>(0x4D41783FB745E42E, c, x, y, z); }
	inline void SetCamRot(Cam c, float x, float y, float z) { invoke<Void>(0x85973643155D0B07, c, x, y, z, 2); }
	inline void SetCamFov(Cam c, float fov) { invoke<Void>(0xB13C14F66A00D047, c, fov); }
	inline void SetCamActive(Cam c, BOOL a) { invoke<Void>(0x026FB97D0A425F84, c, a); }
	inline void RenderScriptCams(BOOL render) { invoke<Void>(0x07E5B515DB0636FC, render, FALSE, 0, TRUE, FALSE, 0); }
	inline void ShakeCam(Cam c, const char *type, float amplitude) { invoke<Void>(0x6A25241C340D3822, c, type, amplitude); }
	inline void InvalidateIdleCam() { invoke<Void>(0xF4F2C0D4EE209E20); }

	// drawing (immediate: call every frame)
	inline void DrawPoly(float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3, int r, int g, int b, int a)
	{
		invoke<Void>(0xAC26716048436851, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b, a);
	}
	inline void DrawRect(float x, float y, float w, float h, int r, int g, int b, int a) { invoke<Void>(0x3A618A217E5154F0, x, y, w, h, r, g, b, a, FALSE); }

	// state
	inline float GetFrameTime() { return invoke<float>(0x15C40837039FFAF7); }
	inline BOOL IsPauseMenuActive() { return invoke<BOOL>(0xB0034A223497FFCB); }
	inline BOOL IsCutsceneActive() { return invoke<BOOL>(0x991251AFC3981F84); }
	inline BOOL IsScreenFadedOut() { return invoke<BOOL>(0xB16FCE9DDC7BA182); }
	inline BOOL IsPlayerSwitchInProgress() { return invoke<BOOL>(0xD9D2CFFF49FAB35F); }

	inline void Notify(const char *text)
	{
		invoke<Void>(0x202709F4C58A0424, "STRING");
		invoke<Void>(0x6C188BE134E074AA, text);
		invoke<int>(0x2ED7843F8F801023, FALSE, FALSE);
	}
}
