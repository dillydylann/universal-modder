// Mario64GTA.asi: Super Mario 64's Mario (libsm64) running around GTA V story mode.
//
// F6 turns the player into Mario and back. Each frame:
//  - GTA's world near Mario is probed into SM64 collision (core/collision), nearby vehicles become moving
//    libsm64 surface objects, and the water height is passed on;
//  - Mario is ticked at 30 Hz from the pad/keyboard (core/mario FixedStep) and drawn with DRAW_POLY,
//    interpolated between ticks;
//  - the player ped, hidden and frozen, stands where Mario is so GTA's peds and cops aim, shoot and swing at
//    it; the health it loses becomes damage to Mario (core/combat);
//  - Mario's punches, kicks, stomps, dives and ground pounds hurt, ragdoll or kill the peds he reaches
//    (core/combat);
//  - a scripted camera does what SM64's Mario cam does (core/camera).
// Everything the script needs lives in <GTA V>\Mario64GTA\: sm64.dll, the user's own ROM (sm64.us.z64) and an
// optional Mario64GTA.ini.
#include <main.h>
#include <windows.h>

#include "core/camera.h"
#include "core/collision.h"
#include "core/combat.h"
#include "core/geom.h"
#include "core/mario.h"
#include "natives.h"
#include "sm64.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace m64;
namespace N = natives;

namespace
{
// ---------------------------------------------------------------------------------------------------------
// settings (Mario64GTA.ini, [Mario] section)
struct Settings
{
	int toggleKey = VK_F6;
	int gangKey = VK_F9;
	std::string rom = "sm64.us.z64";
	float scale = 100.0f;         // SM64 units per metre (Mario is ~1.6 m at 100)
	int probesPerFrame = 250;     // collision probes per frame
	bool bothSides = true;        // draw each triangle both ways round (DRAW_POLY culls one side)
	int hpPerWedge = 20;          // GTA health lost per wedge off Mario's power meter
	int maxWedgesPerHit = 3;
	int wantedOnHit = 2;          // hitting someone raises the wanted level to at least this (0: never)
	int fightBackPercent = 50;    // chance a ped Mario hit (and didn't kill) fights back
	float mouseLook = 6.0f;       // radians per unit of GTA's mouse look normal
	float padLook = 3.2f;         // radians per second at full right stick
	float respawnSeconds = 3.0f;
};

std::string g_dir; // <GTA V>\Mario64GTA\ (with the slash)
Settings g_set;
std::ofstream g_log;

void logLine(const char *s)
{
	if (g_log.is_open())
	{
		g_log << s << '\n';
		g_log.flush();
	}
}

void sm64Print(const char *s)
{
	std::string line = std::string("[libsm64] ") + s;
	logLine(line.c_str());
}

float iniFloat(const char *key, float def, const std::string &ini)
{
	char buf[64];
	char defBuf[64];
	std::snprintf(defBuf, sizeof defBuf, "%g", def);
	GetPrivateProfileStringA("Mario", key, defBuf, buf, sizeof buf, ini.c_str());
	return static_cast<float>(std::atof(buf));
}

void loadSettings()
{
	char exe[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	std::string d(exe);
	d = d.substr(0, d.find_last_of("\\/") + 1) + "Mario64GTA\\";
	g_dir = d;
	const std::string ini = d + "Mario64GTA.ini";
	Settings s;
	s.toggleKey = GetPrivateProfileIntA("Mario", "ToggleKey", s.toggleKey, ini.c_str());
	s.gangKey = GetPrivateProfileIntA("Mario", "GangKey", s.gangKey, ini.c_str());
	char rom[MAX_PATH];
	GetPrivateProfileStringA("Mario", "Rom", s.rom.c_str(), rom, sizeof rom, ini.c_str());
	s.rom = rom;
	s.scale = clampf(iniFloat("Scale", s.scale, ini), 40.0f, 250.0f);
	s.probesPerFrame = std::max(50, static_cast<int>(GetPrivateProfileIntA("Mario", "ProbesPerFrame", s.probesPerFrame, ini.c_str())));
	s.bothSides = GetPrivateProfileIntA("Mario", "DrawBothSides", s.bothSides ? 1 : 0, ini.c_str()) != 0;
	s.hpPerWedge = std::max(1, static_cast<int>(GetPrivateProfileIntA("Mario", "HpPerWedge", s.hpPerWedge, ini.c_str())));
	s.maxWedgesPerHit = std::max(1, static_cast<int>(GetPrivateProfileIntA("Mario", "MaxWedgesPerHit", s.maxWedgesPerHit, ini.c_str())));
	s.wantedOnHit = std::min(5, static_cast<int>(GetPrivateProfileIntA("Mario", "WantedOnHit", s.wantedOnHit, ini.c_str())));
	s.fightBackPercent = GetPrivateProfileIntA("Mario", "FightBackPercent", s.fightBackPercent, ini.c_str());
	s.mouseLook = iniFloat("MouseLook", s.mouseLook, ini);
	s.padLook = iniFloat("PadLook", s.padLook, ini);
	s.respawnSeconds = iniFloat("RespawnSeconds", s.respawnSeconds, ini);
	g_set = s;
}

// ---------------------------------------------------------------------------------------------------------
// keyboard (ScriptHookV calls this on the game's window thread; the script thread picks the presses up)
std::atomic<bool> g_togglePressed{false};
std::atomic<bool> g_gangPressed{false};

void onKeyboard(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow)
{
	if (isUpNow || wasDownBefore)
		return;
	if (static_cast<int>(key) == g_set.toggleKey)
		g_togglePressed = true;
	else if (static_cast<int>(key) == g_set.gangKey)
		g_gangPressed = true;
}

// ---------------------------------------------------------------------------------------------------------
// libsm64
Sm64 g_lib;
bool g_libReady = false;
std::vector<uint8_t> g_rom;
std::vector<uint8_t> g_atlas(4 * SM64_TEXTURE_WIDTH * SM64_TEXTURE_HEIGHT);

bool initLibsm64()
{
	if (g_libReady)
		return true;
	if (!g_lib.load((g_dir + "sm64.dll").c_str()))
	{
		N::Notify("Mario64GTA: couldn't load Mario64GTA\\sm64.dll (run fetch_deps.sh and install.sh)");
		return false;
	}
	std::ifstream f(g_dir + g_set.rom, std::ios::binary);
	if (!f)
	{
		const std::string msg = "Mario64GTA: put your own Super Mario 64 (USA) ROM at Mario64GTA\\" + g_set.rom;
		N::Notify(msg.c_str());
		return false;
	}
	g_rom.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	const RomResult r = normalizeRom(g_rom);
	if (r != RomResult::Ok)
	{
		const std::string msg = std::string("Mario64GTA: ") + romResultText(r);
		N::Notify(msg.c_str());
		g_rom.clear();
		return false;
	}
	g_lib.sm64_register_debug_print_function(sm64Print);
	g_lib.sm64_global_init(g_rom.data(), g_atlas.data());
	g_libReady = true;
	logLine("libsm64 initialised");
	return true;
}

// ---------------------------------------------------------------------------------------------------------
// Mario's state
struct Geometry
{
	std::vector<float> pos, normal, color, uv;
	SM64MarioGeometryBuffers buffers{};
	Geometry()
		: pos(9 * SM64_GEO_MAX_TRIANGLES), normal(9 * SM64_GEO_MAX_TRIANGLES), color(9 * SM64_GEO_MAX_TRIANGLES),
		  uv(6 * SM64_GEO_MAX_TRIANGLES)
	{
		rebind();
	}
	void rebind()
	{
		buffers.position = pos.data();
		buffers.normal = normal.data();
		buffers.color = color.data();
		buffers.uv = uv.data();
	}
};

struct ProxySaved
{
	int maxHealth = 200, health = 200;
};

struct World
{
	bool active = false;
	int32_t mario = -1;
	Frame frame;
	CollisionBuilder builder;
	FixedStep step;
	Geometry cur, prev;
	std::vector<Rgb> colors;
	SM64MarioState state{}, prevState{};
	MarioCam cam;
	Cam camHandle = 0;
	Combat combat;
	std::unordered_map<int, uint32_t> vehicles; // GTA vehicle -> libsm64 surface object
	V3 lastSafe;           // the last place Mario stood on the ground
	float deadFor = 0;     // seconds since Mario died
	float noFloorFor = 0;  // seconds with no collision under Mario (fell through a gap in it)
	ProxySaved saved;
};
World g_w;

constexpr int kProxyHealth = 3000;
const int kProbeFlags = 1 | 16; // map and objects; vehicles are surface objects, peds are skipped

Ped proxyPed() { return N::PlayerPedId(); }

ProbeHit probeWorld(const V3 &from, const V3 &to)
{
	ProbeHit out;
	const int h = N::ShapeTestLosProbeNow(from.x, from.y, from.z, to.x, to.y, to.z, kProbeFlags, proxyPed());
	BOOL hit = FALSE;
	Vector3 end{}, normal{};
	Entity ent = 0;
	N::GetShapeTestResult(h, &hit, &end, &normal, &ent);
	if (hit)
	{
		out.hit = true;
		out.pos = {end.x, end.y, end.z};
		out.normal = {normal.x, normal.y, normal.z};
	}
	return out;
}

V3 toV3(const Vector3 &v) { return {v.x, v.y, v.z}; }

V3 marioGta(const SM64MarioState &s) { return g_w.frame.toGta({s.position[0], s.position[1], s.position[2]}); }

V3 marioGtaInterp()
{
	const float a = clampf(g_w.step.alpha(), 0, 1);
	return lerp(marioGta(g_w.prevState), marioGta(g_w.state), a);
}

float marioHeading(const SM64MarioState &s) { return gtaHeadingFromSmYaw(s.faceAngle); }

void loadStaticSurfaces()
{
	const std::vector<Surface> surfaces = toSurfaces(g_w.builder.tris(), g_w.frame);
	static_assert(sizeof(Surface) == sizeof(SM64Surface), "Surface must match SM64Surface");
	g_lib.sm64_static_surfaces_load(reinterpret_cast<const SM64Surface *>(surfaces.data()),
		static_cast<uint32_t>(surfaces.size()));
}

void dropVehicles()
{
	for (auto &kv : g_w.vehicles)
		g_lib.sm64_surface_object_delete(kv.second);
	g_w.vehicles.clear();
}

// Moves the floating origin to `origin`, shifting everything that lives in SM64 coordinates.
void moveOrigin(const V3 &origin)
{
	const V3 d = g_w.frame.toSm(origin); // the new origin in the old SM64 frame
	g_w.frame.origin = origin;
	for (SM64MarioState *s : {&g_w.state, &g_w.prevState})
	{
		s->position[0] -= d.x;
		s->position[1] -= d.y;
		s->position[2] -= d.z;
	}
	for (Geometry *g : {&g_w.cur, &g_w.prev})
		for (int i = 0; i < 3 * SM64_GEO_MAX_TRIANGLES; ++i)
		{
			g->pos[3 * i] -= d.x;
			g->pos[3 * i + 1] -= d.y;
			g->pos[3 * i + 2] -= d.z;
		}
	dropVehicles(); // recreated next tick; moving them across the jump would fling Mario
}

// ---------------------------------------------------------------------------------------------------------
// the stand-in ped

void hideProxy(Ped p)
{
	g_w.saved.maxHealth = N::GetEntityMaxHealth(p);
	g_w.saved.health = N::GetEntityHealth(p);
	N::SetEntityVisible(p, FALSE);
	N::FreezeEntityPosition(p, TRUE);
	N::SetPedCanRagdoll(p, FALSE);
	N::SetPedSuffersCriticalHits(p, FALSE);
	N::SetPedDiesInWater(p, FALSE);
	N::SetPedMaxHealth(p, kProxyHealth);
	N::SetEntityMaxHealth(p, kProxyHealth);
	N::SetEntityHealth(p, kProxyHealth);
	// cops shoot armed suspects and try to arrest unarmed ones; an invisible pistol keeps them shooting.
	// All controls are disabled while Mario is out, so the player can't fire it.
	const Hash pistol = N::GetHashKey("WEAPON_PISTOL");
	N::GiveWeaponToPed(p, pistol, 0);
	N::SetCurrentPedWeapon(p, pistol, TRUE);
}

void restoreProxy(Ped p, const V3 &at, float headingDeg)
{
	N::FreezeEntityPosition(p, FALSE);
	N::SetEntityCoordsNoOffset(p, at.x, at.y, at.z + 1.0f);
	N::SetEntityHeading(p, headingDeg);
	N::SetEntityVisible(p, TRUE);
	N::SetPedCanRagdoll(p, TRUE);
	N::SetPedSuffersCriticalHits(p, TRUE);
	N::SetPedDiesInWater(p, TRUE);
	N::SetPedMaxHealth(p, g_w.saved.maxHealth);
	N::SetEntityMaxHealth(p, g_w.saved.maxHealth);
	N::SetEntityHealth(p, std::max(101, std::min(g_w.saved.health, g_w.saved.maxHealth)));
}

// ---------------------------------------------------------------------------------------------------------
// on / off

V3 groundUnder(const V3 &p)
{
	const ProbeHit h = probeWorld(p + V3{0, 0, 0.5f}, p - V3{0, 0, 5.0f});
	return h.hit ? h.pos : p - V3{0, 0, 1.0f};
}

bool spawnMario(const V3 &feet, float headingRad)
{
	const V3 s = g_w.frame.toSm(feet + V3{0, 0, 0.1f});
	g_w.mario = g_lib.sm64_mario_create(s.x, s.y, s.z);
	if (g_w.mario < 0)
		return false;
	g_lib.sm64_set_mario_faceangle(g_w.mario, smYawFromGtaHeading(headingRad));
	g_w.state = {};
	g_w.state.position[0] = s.x;
	g_w.state.position[1] = s.y;
	g_w.state.position[2] = s.z;
	g_w.state.faceAngle = smYawFromGtaHeading(headingRad);
	g_w.state.health = 0x880;
	g_w.prevState = g_w.state;
	g_w.cur.buffers.numTrianglesUsed = 0;
	g_w.prev.buffers.numTrianglesUsed = 0;
	g_w.lastSafe = feet;
	g_w.deadFor = 0;
	g_w.noFloorFor = 0;
	return true;
}

void enable()
{
	loadSettings();
	if (!initLibsm64())
		return;
	const Ped p = proxyPed();
	if (N::IsPedInAnyVehicle(p))
	{
		N::Notify("Mario64GTA: get out of the vehicle first");
		return;
	}
	if (N::IsPedDeadOrDying(p))
		return;
	const V3 feet = groundUnder(toV3(N::GetEntityCoords(p)));
	const float heading = N::GetEntityHeading(p) * kPi / 180.0f;

	g_w.frame = Frame{feet, g_set.scale};
	g_w.builder.begin(feet);
	while (!g_w.builder.step(probeWorld, 100000))
	{
	}
	loadStaticSurfaces();
	if (!spawnMario(feet, heading))
	{
		N::Notify("Mario64GTA: libsm64 found no floor here; try somewhere flatter");
		logLine("sm64_mario_create failed");
		return;
	}
	hideProxy(p);
	g_w.step = FixedStep{};
	g_w.combat = Combat{};
	g_w.cam.reset(feet, heading);
	g_w.camHandle = N::CreateCam("DEFAULT_SCRIPTED_CAMERA");
	N::SetCamActive(g_w.camHandle, TRUE);
	N::RenderScriptCams(TRUE);
	g_w.active = true;
	N::Notify("Mario64GTA: it's-a me! (F6 to go back)");
	logLine("Mario on");
}

void disable()
{
	if (!g_w.active)
		return;
	// back where Mario is, unless he's fallen out of the collision; then where he last stood
	const V3 at = g_w.noFloorFor > 0 ? g_w.lastSafe : marioGta(g_w.state);
	const float heading = marioHeading(g_w.state) * 180.0f / kPi;
	dropVehicles();
	if (g_w.mario >= 0)
		g_lib.sm64_mario_delete(g_w.mario);
	g_w.mario = -1;
	N::RenderScriptCams(FALSE);
	if (g_w.camHandle)
		N::DestroyCam(g_w.camHandle);
	g_w.camHandle = 0;
	restoreProxy(proxyPed(), at, heading);
	g_w.active = false;
	logLine("Mario off");
}

// ---------------------------------------------------------------------------------------------------------
// per frame

void keepCollision()
{
	const V3 feet = marioGta(g_w.state);
	CollisionBuilder &b = g_w.builder;
	if (!b.busy())
	{
		const V3 d = feet - b.center();
		if (length2d(d) > 4.0f || std::fabs(d.z) > 3.0f)
			b.begin(feet);
		return;
	}
	if (!b.step(probeWorld, g_set.probesPerFrame))
		return;
	// a finished window: move the origin next to it if Mario has wandered far, then swap the surfaces in
	const V3 o = feet - g_w.frame.origin;
	// not while he's in the air: libsm64 keeps his peak height for fall damage, and moving him would skew it
	if ((length2d(o) > 60.0f || std::fabs(o.z) > 30.0f) && !isAirAction(g_w.state.action))
	{
		moveOrigin(b.center());
		loadStaticSurfaces();
		const SM64MarioState &s = g_w.state;
		g_lib.sm64_set_mario_position(g_w.mario, s.position[0], s.position[1], s.position[2]);
	}
	else
		loadStaticSurfaces();
}

void keepVehicles(const V3 &marioFeet)
{
	static int handles[512];
	const int n = worldGetAllVehicles(handles, 512);
	std::unordered_map<int, uint32_t> keep;
	for (int i = 0; i < n; ++i)
	{
		const Vehicle v = handles[i];
		if (!N::DoesEntityExist(v))
			continue;
		const V3 at = toV3(N::GetEntityCoords(v));
		if (length(at - marioFeet) > 25.0f)
			continue;
		SM64ObjectTransform t{};
		const V3 s = g_w.frame.toSm(at);
		t.position[0] = s.x;
		t.position[1] = s.y;
		t.position[2] = s.z;
		t.eulerRotation[1] = smObjectYawFromGtaHeading(N::GetEntityHeading(v));
		auto it = g_w.vehicles.find(v);
		if (it != g_w.vehicles.end())
		{
			g_lib.sm64_surface_object_move(it->second, &t);
			keep[v] = it->second;
			g_w.vehicles.erase(it);
			continue;
		}
		Vector3 mn{}, mx{};
		N::GetModelDimensions(N::GetEntityModel(v), &mn, &mx);
		std::vector<Surface> box = vehicleBox(toV3(mn), toV3(mx), g_w.frame.scale);
		SM64SurfaceObject obj{};
		obj.transform = t;
		obj.surfaceCount = static_cast<uint32_t>(box.size());
		obj.surfaces = reinterpret_cast<SM64Surface *>(box.data()); // libsm64 copies them
		keep[v] = g_lib.sm64_surface_object_create(&obj);
	}
	dropVehicles(); // whatever is left went away or out of range
	g_w.vehicles.swap(keep);
}

std::vector<PedInfo> pedsNear(const V3 &at, float radius)
{
	static int handles[1024];
	const int n = worldGetAllPeds(handles, 1024);
	const Ped me = proxyPed();
	std::vector<PedInfo> out;
	for (int i = 0; i < n; ++i)
	{
		const Ped p = handles[i];
		if (p == me || !N::DoesEntityExist(p) || N::IsPedDeadOrDying(p))
			continue;
		const V3 pos = toV3(N::GetEntityCoords(p));
		if (length(pos - at) <= radius)
			out.push_back({p, pos});
	}
	return out;
}

void raiseWanted()
{
	if (g_set.wantedOnHit <= 0)
		return;
	const Player pl = N::PlayerId();
	if (N::GetPlayerWantedLevel(pl) < g_set.wantedOnHit)
	{
		N::SetPlayerWantedLevel(pl, g_set.wantedOnHit);
		N::SetPlayerWantedLevelNow(pl);
	}
}

void applyHits(const std::vector<PedHit> &hits)
{
	if (hits.empty())
		return;
	const Ped me = proxyPed();
	for (const PedHit &h : hits)
	{
		const Ped p = h.id;
		if (!N::DoesEntityExist(p))
			continue;
		if (h.kill)
			N::SetEntityHealth(p, 0);
		else
			N::ApplyDamageToPed(p, h.damage);
		N::SetPedToRagdoll(p, h.kill ? 4000 : 1200 + 15 * h.damage);
		N::ApplyForceToEntity(p, h.push.x, h.push.y, h.push.z);
		if (!h.kill && !N::IsPedDeadOrDying(p) && (std::rand() % 100) < g_set.fightBackPercent)
		{
			N::GiveWeaponToPed(p, N::GetHashKey("WEAPON_PISTOL"), 60);
			N::TaskCombatPed(p, me);
			N::SetPedKeepTask(p, TRUE);
		}
		char line[96];
		std::snprintf(line, sizeof line, "hit ped %d: %s, %d%s", p, moveName(h.move), h.damage, h.kill ? ", killed" : "");
		logLine(line);
	}
	raiseWanted();
}

void tickMario(const SM64MarioInputs &in)
{
	const V3 feet = marioGta(g_w.state);
	float water = 0;
	if (N::GetWaterHeight(feet.x, feet.y, feet.z + 2.0f, &water))
		g_lib.sm64_set_mario_water_level(g_w.mario, static_cast<int>((water - g_w.frame.origin.z) * g_w.frame.scale));
	else
		g_lib.sm64_set_mario_water_level(g_w.mario, -1000000);
	keepVehicles(feet);

	std::swap(g_w.cur, g_w.prev);
	g_w.cur.rebind();
	g_w.prev.rebind();
	g_w.prevState = g_w.state;
	g_lib.sm64_mario_tick(g_w.mario, &in, &g_w.state, &g_w.cur.buffers);

	// colours once per tick, not per frame
	const int n = g_w.cur.buffers.numTrianglesUsed;
	g_w.colors.resize(n);
	const V3 sun = normalize(V3{0.3f, 0.4f, 1.0f});
	for (int i = 0; i < n; ++i)
	{
		const float *nm = &g_w.cur.normal[9 * i];
		const V3 normal = Frame::dirToGta({nm[0], nm[1], nm[2]});
		g_w.colors[i] = triangleColor(&g_w.cur.color[9 * i], &g_w.cur.uv[6 * i], g_atlas.data(), normal, sun, 0.45f);
	}

	const uint32_t act = g_w.state.action;
	const V3 now = marioGta(g_w.state);
	if (!isAirAction(act) && g_w.state.health >= 0x100)
		g_w.lastSafe = now;

	// Mario on peds
	const CombatConfig &cc = g_w.combat.config();
	const auto attack = [&](const PedInfo &p) {
		const V3 s = g_w.frame.toSm(p.pos + V3{0, 0, cc.pedHitHeight});
		return g_lib.sm64_mario_attack(g_w.mario, s.x, s.y, s.z, 0.0f);
	};
	applyHits(g_w.combat.tick(act, g_w.state.velocity[1], now, marioHeading(g_w.state), pedsNear(now, 8.0f), attack));
}

// GTA's damage to the stand-in becomes damage to Mario, from whoever is most likely to have done it
void takeDamage()
{
	const Ped me = proxyPed();
	const int hp = N::GetEntityHealth(me);
	const int lost = kProxyHealth - hp;
	N::SetEntityHealth(me, kProxyHealth);
	N::ClearEntityLastDamageEntity(me);
	const int wedges = wedgesForGtaDamage(lost, g_set.hpPerWedge, g_set.maxWedgesPerHit);
	if (wedges <= 0 || g_w.state.health < 0x100 || g_w.state.invincTimer > 0)
		return;
	const V3 at = marioGta(g_w.state);
	V3 src = at + gtaForward(marioHeading(g_w.state)); // default: from in front
	float best = 1e9f;
	for (const PedInfo &p : pedsNear(at, 80.0f))
	{
		const bool melee = N::IsPedInMeleeCombat(p.id) && length(p.pos - at) < 3.0f;
		if (!melee && !N::IsPedShooting(p.id))
			continue;
		const float d = length(p.pos - at);
		if (d < best)
		{
			best = d;
			src = p.pos;
		}
	}
	const V3 s = g_w.frame.toSm(src);
	g_lib.sm64_mario_take_damage(g_w.mario, static_cast<uint32_t>(wedges), 0, s.x, s.y, s.z);
	char line[80];
	std::snprintf(line, sizeof line, "Mario hit: %d hp -> %d wedge(s)", lost, wedges);
	logLine(line);
}

void placeProxy()
{
	const Ped me = proxyPed();
	const V3 at = marioGtaInterp();
	N::SetEntityCoordsNoOffset(me, at.x, at.y, at.z + 1.0f); // a ped's coords are about 1 m above its feet
	N::SetEntityHeading(me, marioHeading(g_w.state) * 180.0f / kPi);
	N::SetEntityVelocity(me, 0, 0, 0);
	N::SetEntityVisible(me, FALSE);
}

void drawMario()
{
	const int n = g_w.cur.buffers.numTrianglesUsed;
	const bool interp = g_w.prev.buffers.numTrianglesUsed == n;
	const float a = clampf(g_w.step.alpha(), 0, 1);
	for (int i = 0; i < n && i < static_cast<int>(g_w.colors.size()); ++i)
	{
		V3 v[3];
		for (int k = 0; k < 3; ++k)
		{
			const float *c = &g_w.cur.pos[9 * i + 3 * k];
			V3 s{c[0], c[1], c[2]};
			if (interp)
			{
				const float *p = &g_w.prev.pos[9 * i + 3 * k];
				s = lerp(V3{p[0], p[1], p[2]}, s, a);
			}
			v[k] = g_w.frame.toGta(s);
		}
		const Rgb &c = g_w.colors[i];
		N::DrawPoly(v[0].x, v[0].y, v[0].z, v[1].x, v[1].y, v[1].z, v[2].x, v[2].y, v[2].z, c.r, c.g, c.b, 255);
		if (g_set.bothSides)
			N::DrawPoly(v[0].x, v[0].y, v[0].z, v[2].x, v[2].y, v[2].z, v[1].x, v[1].y, v[1].z, c.r, c.g, c.b, 255);
	}
}

void drawShadow()
{
	const V3 at = marioGtaInterp();
	const V3 s = g_w.frame.toSm(at);
	const float floorSm = g_lib.sm64_surface_find_floor_height(s.x, s.y + 10.0f, s.z);
	if (floorSm <= -100000.0f) // libsm64's "no floor" (FLOOR_LOWER_LIMIT)
		return;
	const float floorZ = g_w.frame.toGta({s.x, floorSm, s.z}).z + 0.03f;
	const float h = at.z - floorZ;
	if (h > 20.0f)
		return;
	const float r = 0.38f * clampf(1.0f - h / 20.0f, 0.3f, 1.0f);
	const V3 c{at.x, at.y, floorZ};
	const int seg = 12;
	for (int i = 0; i < seg; ++i)
	{
		const float a0 = 2 * kPi * i / seg, a1 = 2 * kPi * (i + 1) / seg;
		const V3 p0 = c + V3{std::cos(a0) * r, std::sin(a0) * r, 0};
		const V3 p1 = c + V3{std::cos(a1) * r, std::sin(a1) * r, 0};
		N::DrawPoly(c.x, c.y, c.z, p0.x, p0.y, p0.z, p1.x, p1.y, p1.z, 0, 0, 0, 120);
		N::DrawPoly(c.x, c.y, c.z, p1.x, p1.y, p1.z, p0.x, p0.y, p0.z, 0, 0, 0, 120);
	}
}

// SM64's power meter: 8 wedges, blue at 7-8, green at 5-6, yellow at 3-4, red at 1-2
void drawPowerMeter()
{
	const int wedges = std::max(0, std::min(8, g_w.state.health >> 8));
	int r = 40, g = 120, b = 255;
	if (wedges <= 2)
		r = 230, g = 40, b = 30;
	else if (wedges <= 4)
		r = 250, g = 210, b = 30;
	else if (wedges <= 6)
		r = 60, g = 200, b = 60;
	N::DrawRect(0.5f, 0.06f, 0.17f, 0.045f, 0, 0, 0, 140);
	for (int i = 0; i < 8; ++i)
	{
		const float x = 0.5f - 0.0735f + i * 0.021f;
		if (i < wedges)
			N::DrawRect(x, 0.06f, 0.017f, 0.03f, r, g, b, 230);
		else
			N::DrawRect(x, 0.06f, 0.017f, 0.03f, 60, 60, 60, 160);
	}
}

void updateCamera(float dt)
{
	CamInput in;
	const float lx = N::GetDisabledControlNormal(0, 1); // INPUT_LOOK_LR
	const float ly = N::GetDisabledControlNormal(0, 2); // INPUT_LOOK_UD
	if (N::IsUsingKeyboardAndMouse())
	{
		in.turn = lx * g_set.mouseLook * 0.1f;
		in.tilt = ly * g_set.mouseLook * 0.1f;
	}
	else
	{
		in.turn = lx * g_set.padLook * dt;
		in.tilt = ly * g_set.padLook * dt;
	}
	if (N::IsDisabledControlJustPressed(0, 241)) // INPUT_CURSOR_SCROLL_UP
		in.zoom = -1;
	if (N::IsDisabledControlJustPressed(0, 242)) // INPUT_CURSOR_SCROLL_DOWN
		in.zoom = 1;
	in.recenter = N::IsDisabledControlJustPressed(0, 26) != 0; // INPUT_LOOK_BEHIND (C / R3)

	const SM64MarioState &s = g_w.state;
	const float speed = std::hypot(s.velocity[0], s.velocity[2]) * 30.0f / g_w.frame.scale;
	const Probe probe = [](const V3 &a, const V3 &b) { return probeWorld(a, b); };
	const CamPose pose = g_w.cam.update(dt, marioGtaInterp(), marioHeading(s), speed, !isAirAction(s.action), in, &probe);
	N::SetCamCoord(g_w.camHandle, pose.pos.x, pose.pos.y, pose.pos.z);
	N::SetCamRot(g_w.camHandle, pose.pitchDeg, 0.0f, pose.headingDeg);
	N::SetCamFov(g_w.camHandle, pose.fovDeg);
	N::InvalidateIdleCam();
}

SM64MarioInputs readInputs()
{
	SM64MarioInputs in{};
	float x = N::GetDisabledControlNormal(0, 30); // INPUT_MOVE_LR: right is +
	float y = N::GetDisabledControlNormal(0, 31); // INPUT_MOVE_UD: back is +, as SM64's stick
	const float m = std::sqrt(x * x + y * y);
	if (m > 1.0f)
		x /= m, y /= m;
	in.stickX = x;
	in.stickY = y;
	in.buttonA = N::IsDisabledControlPressed(0, 22) ? 1 : 0; // INPUT_JUMP
	in.buttonB = (N::IsDisabledControlPressed(0, 24) || N::IsDisabledControlPressed(0, 140)) ? 1 : 0; // attack / melee
	in.buttonZ = (N::IsDisabledControlPressed(0, 36) || N::IsDisabledControlPressed(0, 25)) ? 1 : 0;   // duck / aim
	// SM64 steers relative to the camera: camLook is from the camera to Mario, in SM64 x/z
	const V3 camSm = g_w.frame.toSm(g_w.cam.position());
	in.camLookX = g_w.state.position[0] - camSm.x;
	in.camLookZ = g_w.state.position[2] - camSm.z;
	return in;
}

void respawnIfDead(float dt)
{
	// no collision under him for a while: he fell through a gap in it, which counts as a death
	const V3 s{g_w.state.position[0], g_w.state.position[1], g_w.state.position[2]};
	if (g_lib.sm64_surface_find_floor_height(s.x, s.y + 10.0f, s.z) <= -100000.0f)
		g_w.noFloorFor += dt;
	else
		g_w.noFloorFor = 0;
	if (g_w.state.health >= 0x100 && g_w.noFloorFor < 2.5f)
	{
		g_w.deadFor = 0;
		return;
	}
	g_w.deadFor += dt;
	if (g_w.deadFor < g_set.respawnSeconds)
		return;
	g_lib.sm64_mario_delete(g_w.mario);
	g_w.mario = -1;
	const float heading = marioHeading(g_w.state);
	// the collision window has followed him away (a fall, say): rebuild it round the respawn point first, or
	// libsm64 finds no floor there and won't create him
	moveOrigin(g_w.lastSafe);
	g_w.builder.begin(g_w.lastSafe);
	while (!g_w.builder.step(probeWorld, 100000))
	{
	}
	loadStaticSurfaces();
	if (!spawnMario(g_w.lastSafe, heading))
	{
		N::Notify("Mario64GTA: couldn't respawn Mario here");
		g_w.noFloorFor = 1; // so disable() puts the player back at lastSafe, not where Mario fell
		disable();
		return;
	}
	g_w.cam.reset(g_w.lastSafe, heading);
	logLine("Mario respawned");
}

void spawnGang()
{
	if (!g_w.active)
		return;
	const Hash model = N::GetHashKey("g_m_y_ballaeast_01");
	N::RequestModel(model);
	for (int i = 0; i < 200 && !N::HasModelLoaded(model); ++i)
		scriptWait(0);
	if (!N::HasModelLoaded(model))
		return;
	const V3 at = marioGta(g_w.state);
	const Ped me = proxyPed();
	const Hash pistol = N::GetHashKey("WEAPON_PISTOL");
	for (int i = 0; i < 3; ++i)
	{
		const float a = marioHeading(g_w.state) + (i - 1) * 0.5f;
		const V3 p = groundUnder(at + gtaForward(a) * 9.0f + V3{0, 0, 1.0f});
		Ped ped = N::CreatePed(model, p.x, p.y, p.z + 1.0f, a * 180.0f / kPi + 180.0f);
		N::GiveWeaponToPed(ped, pistol, 200);
		N::SetPedAccuracy(ped, 20);
		N::SetPedCombatAttributes(ped, 46, TRUE); // always fight
		N::TaskCombatPed(ped, me);
		N::SetPedKeepTask(ped, TRUE);
		N::SetPedAsNoLongerNeeded(&ped);
	}
	N::SetModelAsNoLongerNeeded(model);
	logLine("spawned a gang");
}

void frame()
{
	if (g_togglePressed.exchange(false))
	{
		if (g_w.active)
			disable();
		else
			enable();
	}
	if (g_gangPressed.exchange(false))
		spawnGang();
	if (!g_w.active)
		return;

	const Ped me = proxyPed();
	if (!N::DoesEntityExist(me) || N::IsPedDeadOrDying(me) || N::IsPlayerSwitchInProgress() || N::IsCutsceneActive())
	{
		disable();
		return;
	}

	if (N::IsPauseMenuActive())
		return;
	N::DisableAllControlActions(0);
	N::EnableControlAction(0, 199); // INPUT_FRONTEND_PAUSE
	N::EnableControlAction(0, 200); // INPUT_FRONTEND_PAUSE_ALTERNATE
	const float dt = clampf(N::GetFrameTime(), 0.0f, 0.25f);

	keepCollision();
	takeDamage();
	const SM64MarioInputs in = readInputs();
	const int ticks = g_w.step.advance(dt);
	for (int i = 0; i < ticks && g_w.mario >= 0; ++i)
		tickMario(in);
	respawnIfDead(dt);
	if (!g_w.active)
		return;
	placeProxy();
	updateCamera(dt);
	drawShadow();
	drawMario();
	drawPowerMeter();
}

void scriptMain()
{
	loadSettings();
	g_log.open(g_dir + "Mario64GTA.log", std::ios::trunc);
	logLine("Mario64GTA loaded");
	for (;;)
	{
		frame();
		scriptWait(0);
	}
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		scriptRegister(module, scriptMain);
		keyboardHandlerRegister(onKeyboard);
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		scriptUnregister(module);
		keyboardHandlerUnregister(onKeyboard);
	}
	return TRUE;
}
