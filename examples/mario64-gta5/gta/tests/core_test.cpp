// Tests for core/ without GTA, a ROM or Windows:
//   g++ -std=c++17 -O1 -I../src tests/core_test.cpp ../src/core/*.cpp -o core_test && ./core_test
// With -DWITH_LIBSM64 (plus libsm64's include dir and library) it also loads the probe-built collision into the
// real libsm64 and checks floors, walls and a rotated vehicle box from libsm64's side. Mario himself needs the
// ROM, so he isn't created here.
#include "core/camera.h"
#include "core/collision.h"
#include "core/combat.h"
#include "core/mario.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#ifdef WITH_LIBSM64
#include <libsm64.h>
#endif

using namespace m64;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                    \
	do                                                                                 \
	{                                                                                  \
		if (cond)                                                                      \
			++g_pass;                                                                  \
		else                                                                           \
		{                                                                              \
			++g_fail;                                                                  \
			std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                \
		}                                                                              \
	} while (0)
static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

// --- a tiny GTA stand-in: flat ground at z=0, a 4 m tall building x 5..9, y -3..3, a 0.2 m curb y > 10 and a
// bridge deck 3 m up
struct Box
{
	V3 mn, mx;
};
static const Box kBuilding{{5, -3, 0}, {9, 3, 4}};
static const Box kCurb{{-50, 10, 0}, {50, 50, 0.2f}};
static const Box kBridge{{-10, -2, 3}, {-6, 2, 3.5f}}; // a slab Mario can walk under

static bool rayBox(const V3 &o, const V3 &d, const Box &b, float &t, V3 &n)
{
	float t0 = 0, t1 = 1;
	V3 nn;
	const float oa[3] = {o.x, o.y, o.z}, da[3] = {d.x, d.y, d.z};
	const float mn[3] = {b.mn.x, b.mn.y, b.mn.z}, mx[3] = {b.mx.x, b.mx.y, b.mx.z};
	for (int k = 0; k < 3; ++k)
	{
		if (std::fabs(da[k]) < 1e-9f)
		{
			if (oa[k] < mn[k] || oa[k] > mx[k])
				return false;
			continue;
		}
		float ta = (mn[k] - oa[k]) / da[k], tb = (mx[k] - oa[k]) / da[k];
		float sign = -1;
		if (ta > tb)
		{
			std::swap(ta, tb);
			sign = 1;
		}
		if (ta > t0)
		{
			t0 = ta;
			nn = V3{0, 0, 0};
			(k == 0 ? nn.x : k == 1 ? nn.y : nn.z) = sign;
		}
		if (tb < t1)
			t1 = tb;
		if (t0 > t1)
			return false;
	}
	if (t0 <= 0) // starts inside: GTA's probes don't report the inside of a shape either
		return false;
	t = t0;
	n = nn;
	return true;
}

static ProbeHit fakeWorld(const V3 &from, const V3 &to)
{
	const V3 d = to - from;
	ProbeHit best;
	float bestT = 2;
	if (d.z < 0 && from.z >= 0 && to.z <= 0)
	{
		bestT = from.z / -d.z;
		best = {true, from + d * bestT, {0, 0, 1}};
	}
	for (const Box *b : {&kBuilding, &kCurb, &kBridge})
	{
		float t;
		V3 n;
		if (rayBox(from, d, *b, t, n) && t < bestT)
		{
			bestT = t;
			best = {true, from + d * t, n};
		}
	}
	return best;
}

static void testFrame()
{
	Frame f;
	f.origin = {100, -200, 30};
	const V3 g{103.5f, -190.25f, 31.0f};
	const V3 back = f.toGta(f.toSm(g));
	CHECK(near(back.x, g.x, 1e-3f) && near(back.y, g.y, 1e-3f) && near(back.z, g.z, 1e-3f));
	CHECK(near(f.toSm({100, -200, 31}).y, 100, 1e-3f)); // up is up
	for (float a : {0.0f, 0.7f, -2.0f, 3.0f})
	{
		const V3 smFwd{std::sin(a), 0, std::cos(a)};
		const V3 g1 = Frame::dirToGta(smFwd), g2 = gtaForward(gtaHeadingFromSmYaw(a));
		CHECK(near(g1.x, g2.x, 1e-4f) && near(g1.y, g2.y, 1e-4f));
		CHECK(near(wrapAngle(smYawFromGtaHeading(gtaHeadingFromSmYaw(a)) - a), 0, 1e-4f));
	}
	CHECK(near(headingOf(gtaForward(1.0f)), 1.0f, 1e-5f));
}

static std::vector<Tri> buildFake(const V3 &center, CollisionBuilder &b)
{
	b.begin(center);
	int frames = 0;
	while (!b.step(fakeWorld, 200))
		++frames;
	CHECK(frames >= 2); // the work is spread over several frames
	return b.tris();
}

static void testCollision()
{
	CollisionBuilder b;
	const std::vector<Tri> tris = buildFake({0, 0, 0}, b);
	CHECK(!b.busy() && b.pending() == 0);
	int floors = 0, roofs = 0, stepWalls = 0, probeWalls = 0, curbSteps = 0, underBridge = 0, bridgeWalls = 0;
	for (const Tri &t : tris)
	{
		if (t.n.z > 0.9f)
		{
			++floors;
			if (t.a.z > 3.9f)
				++roofs;
			if (t.a.x <= -6 && t.a.x >= -10 && t.a.y >= -2 && t.a.y <= 2 && t.b.x <= -6 && t.b.x >= -10)
				underBridge += t.a.z < 0.01f ? 1 : -1000;
		}
		else if (near(t.n.x, -1, 1e-3f) && near(t.a.x, 5, 1e-3f))
		{
			// the building's west face: from the roof-to-ground step and from the horizontal probes
			if (near(std::fmin(std::fmin(t.a.z, t.b.z), t.c.z), 0, 1e-3f) && near(std::fmax(std::fmax(t.a.z, t.b.z), t.c.z), 4, 1e-3f))
				++stepWalls;
			else
				++probeWalls;
		}
		else if (near(t.n.y, -1, 1e-3f) && near(t.a.y, 10, 1e-3f))
			++curbSteps;
		else if (std::fabs(t.n.z) < 0.1f && t.a.x < -5.5f && t.a.x > -10.5f && std::fabs(t.a.y) < 2.5f)
			++bridgeWalls;
	}
	CHECK(floors >= 2 * 24 * 24 - 2);
	CHECK(roofs > 0);
	CHECK(stepWalls >= 2 * 6);  // the whole 6 m face, one quad per cell
	CHECK(probeWalls > 0);
	CHECK(curbSteps == 0);      // 0.2 m is a step Mario walks up, not a wall
	CHECK(underBridge > 0);     // under the bridge is the ground...
	CHECK(bridgeWalls == 0);    // ...and it doesn't wall him off

	Frame f;
	f.origin = {0, 0, 0};
	const std::vector<Surface> s = toSurfaces(tris, f);
	CHECK(s.size() == tris.size());
	// SM64's normal (v2-v1)x(v3-v2) must agree with the intended one
	int agree = 0;
	for (size_t i = 0; i < s.size(); ++i)
	{
		const V3 p1(float(s[i].v[0][0]), float(s[i].v[0][1]), float(s[i].v[0][2]));
		const V3 p2(float(s[i].v[1][0]), float(s[i].v[1][1]), float(s[i].v[1][2]));
		const V3 p3(float(s[i].v[2][0]), float(s[i].v[2][1]), float(s[i].v[2][2]));
		if (dot(cross(p2 - p1, p3 - p2), Frame::dirToSm(tris[i].n)) > 0)
			++agree;
	}
	CHECK(agree == static_cast<int>(s.size()));

	const std::vector<Surface> box = vehicleBox({-1, -2.5f, 0}, {1, 2.5f, 1.5f}, 100);
	CHECK(box.size() == 12);
}

static void testCamera()
{
	MarioCam cam;
	const CamInput none;
	// standing still: the camera stays where it was put
	cam.reset({0, 0, 0}, kPi / 2);
	CamPose p = cam.update(1.0f / 60, {0, 0, 0}, kPi / 2, 0, true, none, nullptr);
	CHECK(near(cam.yaw(), kPi / 2, 1e-4f));
	CHECK(p.pitchDeg < 0); // looking down at Mario
	// running north while the camera looks west: it swings round behind him
	cam.reset({0, 0, 0}, kPi / 2);
	V3 feet{0, 0, 0};
	for (int i = 0; i < 180; ++i)
	{
		feet.y += 9.6f / 60;
		cam.update(1.0f / 60, feet, 0, 9.6f, true, none, nullptr);
	}
	CHECK(std::fabs(wrapAngle(cam.yaw())) < 0.2f);
	// running straight at the camera: it doesn't turn round
	cam.reset({0, 0, 0}, 0);
	for (int i = 0; i < 120; ++i)
		cam.update(1.0f / 60, {0, -i * 0.16f, 0}, kPi, 9.6f, true, none, nullptr);
	CHECK(std::fabs(wrapAngle(cam.yaw())) < 0.05f);
	// orbiting by hand, then auto-follow returns after the idle time
	cam.reset({0, 0, 0}, 0);
	CamInput look;
	look.turn = 0.05f;
	for (int i = 0; i < 20; ++i)
		cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, look, nullptr);
	const float turned = cam.yaw();
	CHECK(turned < -0.5f); // mouse right turns the view right (heading decreases)
	// recenter snaps back behind Mario
	CamInput rc;
	rc.recenter = true;
	cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, rc, nullptr);
	for (int i = 0; i < 60; ++i)
		cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, none, nullptr);
	CHECK(std::fabs(cam.yaw()) < 0.02f);
	// zoom levels
	CamInput zin;
	zin.zoom = -1;
	cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, zin, nullptr);
	CHECK(cam.zoom() == 0);
	// a wall behind Mario pulls the camera in
	const Probe wall = [](const V3 &from, const V3 &to) {
		ProbeHit h;
		if (to.y < -2 && from.y > -2)
		{
			const float t = (from.y + 2) / (from.y - to.y);
			h = {true, from + (to - from) * t, {0, 1, 0}};
		}
		return h;
	};
	cam.reset({0, 0, 0}, 0);
	p = cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, none, &wall);
	CHECK(p.pos.y > -2 && p.pos.y < -0.5f);
	// a jump doesn't drag the camera up; a fall below the old ground does pull it down
	cam.reset({0, 0, 0}, 0);
	const float z0 = cam.update(1.0f / 60, {0, 0, 0}, 0, 0, true, none, nullptr).pos.z;
	float z1 = z0;
	for (int i = 0; i < 30; ++i)
		z1 = cam.update(1.0f / 60, {0, 0, 1.5f}, 0, 0, false, none, nullptr).pos.z;
	CHECK(near(z1, z0, 0.05f));
	for (int i = 0; i < 120; ++i)
		z1 = cam.update(1.0f / 60, {0, 0, -5}, 0, 0, false, none, nullptr).pos.z;
	CHECK(z1 < z0 - 4);
}

static void testCombat()
{
	auto always = [](const PedInfo &) { return true; };
	auto never = [](const PedInfo &) { return false; };
	{
		Combat c;
		const std::vector<PedInfo> peds{{1, {0, 1.0f, 1.0f}}, {2, {0, 3.0f, 1.0f}}};
		int asked = 0;
		auto counting = [&](const PedInfo &) { ++asked; return true; };
		std::vector<PedHit> h = c.tick(ACT_PUNCHING, 0, {0, 0, 0}, 0, peds, counting);
		CHECK(h.size() == 1 && h[0].id == 1 && h[0].move == Move::Punch && h[0].damage == 25 && !h[0].kill);
		CHECK(asked == 1);                 // the far ped was never offered to libsm64
		CHECK(h[0].push.y > 0);            // pushed away from Mario
		h = c.tick(ACT_PUNCHING, 0, {0, 0, 0}, 0, peds, always);
		CHECK(h.empty());                  // cooldown
		CHECK(c.tick(ACT_IDLE, 0, {0, 0, 0}, 0, peds, always).empty()); // not attacking
		CHECK(c.tick(ACT_JUMP_KICK, 0, {0, 0, 0}, 0, peds, never).empty()); // libsm64 says it missed
	}
	{
		Combat c;
		std::vector<PedHit> h = c.tick(ACT_SLIDE_KICK, 0, {0, 0, 0}, 0, {{7, {0.5f, 0.5f, 0.9f}}}, always);
		CHECK(h.size() == 1 && h[0].move == Move::SlideKick && h[0].damage == 40);
		// a jump coming down on someone is a stomp
		h = c.tick(0x01000880 /* a plain jump */, -20.0f, {5, 0, 1.6f}, 0, {{8, {5, 0.3f, 1.0f}}}, always);
		CHECK(h.size() == 1 && h[0].move == Move::Stomp);
	}
	{
		// ground pound landing: a shockwave that falls off with distance and stops at its radius
		Combat c;
		const std::vector<PedInfo> peds{{1, {0, 2.0f, 1}}, {2, {0, 3.0f, 1}}, {3, {0, 6.0f, 1}}};
		CHECK(c.tick(ACT_GROUND_POUND, -50.0f, {0, 0, 3}, 0, peds, never).empty());
		std::vector<PedHit> h = c.tick(ACT_GROUND_POUND_LAND, 0, {0, 0, 0}, 0, peds, never);
		CHECK(h.size() == 2);
		if (h.size() == 2)
		{
			CHECK(h[0].move == Move::Shockwave && h[1].move == Move::Shockwave);
			CHECK(h[0].damage > h[1].damage);
			CHECK(h[1].damage >= 30);
		}
		CHECK(c.tick(ACT_GROUND_POUND_LAND, 0, {0, 0, 0}, 0, peds, never).empty()); // once per landing
	}
	{
		// falling onto someone in a ground pound: big damage, and sometimes it kills outright
		int kills = 0;
		for (uint32_t seed = 0; seed < 400; ++seed)
		{
			Combat c({}, seed);
			const std::vector<PedHit> h = c.tick(ACT_GROUND_POUND, -50.0f, {0, 0, 1.8f}, 0, {{1, {0, 0.2f, 1}}}, always);
			CHECK(h.size() == 1 && h[0].move == Move::GroundPound && h[0].damage == 100);
			kills += !h.empty() && h[0].kill;
		}
		CHECK(kills > 120 && kills < 280); // about half
	}
	CHECK(wedgesForGtaDamage(0) == 0);
	CHECK(wedgesForGtaDamage(5) == 1);
	CHECK(wedgesForGtaDamage(40) == 2);
	CHECK(wedgesForGtaDamage(500) == 3);
	CHECK(classify(ACT_MOVE_PUNCHING, 0) == Move::Punch);
	CHECK(classify(ACT_DIVE, -10) == Move::Dive);
	CHECK(classify(ACT_IDLE, 0) == Move::None);
}

static void testRomAndTick()
{
	std::vector<uint8_t> rom(8u * 1024 * 1024, 0);
	const uint8_t z64[4] = {0x80, 0x37, 0x12, 0x40};
	std::memcpy(rom.data(), z64, 4);
	std::memcpy(rom.data() + 0x20, "SUPER MARIO 64      ", 20);
	rom[0x3E] = 'E';
	rom[0x1000] = 0xAB;
	std::vector<uint8_t> v64 = rom;
	for (size_t i = 0; i < v64.size(); i += 2)
		std::swap(v64[i], v64[i + 1]);
	std::vector<uint8_t> n64 = rom;
	for (size_t i = 0; i < n64.size(); i += 4)
	{
		std::swap(n64[i], n64[i + 3]);
		std::swap(n64[i + 1], n64[i + 2]);
	}
	std::vector<uint8_t> a = rom;
	CHECK(normalizeRom(a) == RomResult::Ok);
	CHECK(normalizeRom(v64) == RomResult::Ok && v64 == rom);
	CHECK(normalizeRom(n64) == RomResult::Ok && n64 == rom);
	std::vector<uint8_t> eu = rom;
	eu[0x3E] = 'P';
	CHECK(normalizeRom(eu) == RomResult::NotUs);
	std::vector<uint8_t> other = rom;
	std::memcpy(other.data() + 0x20, "ZELDA", 5);
	CHECK(normalizeRom(other) == RomResult::NotSm64);
	std::vector<uint8_t> small(1024);
	CHECK(normalizeRom(small) == RomResult::WrongSize);

	FixedStep fs;
	int ticks = 0;
	for (int i = 0; i < 144; ++i) // one second at 144 fps
		ticks += fs.advance(1.0f / 144);
	CHECK(ticks == 29 || ticks == 30);
	CHECK(fs.alpha() >= 0 && fs.alpha() < 1);
	CHECK(fs.advance(5.0f) == 4); // a hitch: capped
	CHECK(fs.alpha() < 1);

	const float red[9] = {1, 0, 0, 1, 0, 0, 1, 0, 0}, noUv[6] = {1, 1, 1, 1, 1, 1};
	const Rgb lit = triangleColor(red, noUv, nullptr, {0, 0, 1}, {0, 0, 1}, 0.5f);
	const Rgb dark = triangleColor(red, noUv, nullptr, {0, 0, -1}, {0, 0, 1}, 0.5f);
	CHECK(lit.r == 255 && lit.g == 0 && dark.r == 128);
	std::vector<uint8_t> atlas(4 * kAtlasWidth * kAtlasHeight, 0);
	for (size_t i = 0; i < atlas.size(); i += 4)
		atlas[i + 1] = atlas[i + 3] = 255; // opaque green
	const float uv[6] = {0.1f, 0.1f, 0.2f, 0.1f, 0.1f, 0.2f};
	const Rgb tex = triangleColor(red, uv, atlas.data(), {0, 0, 1}, {0, 0, 1}, 0.5f);
	CHECK(tex.r == 0 && tex.g == 255);
}

#ifdef WITH_LIBSM64
static void testLibsm64()
{
	Frame f;
	f.origin = {0, 0, 0};
	CollisionBuilder b;
	const std::vector<Surface> s = toSurfaces(buildFake({0, 0, 0}, b), f);
	static_assert(sizeof(Surface) == sizeof(SM64Surface), "Surface must match SM64Surface");
	sm64_static_surfaces_load(reinterpret_cast<const SM64Surface *>(s.data()), static_cast<uint32_t>(s.size()));
	auto floorAt = [&](float x, float y, float z) {
		const V3 p = f.toSm({x, y, z});
		return f.toGta({0, sm64_surface_find_floor_height(p.x, p.y, p.z), 0}).z;
	};
	CHECK(near(floorAt(0, 0, 1), 0, 0.02f));
	CHECK(near(floorAt(7, 0, 6), 4, 0.02f));  // the roof
	CHECK(near(floorAt(0, 11, 1), 0.2f, 0.02f)); // the curb
	CHECK(near(floorAt(-8, 0, 1), 0, 0.02f));    // under the bridge
	// Mario walking east into the building's west face is pushed back out
	V3 p = f.toSm({4.9f, 0, 0.0f});
	float x = p.x, y = p.y, z = p.z;
	const int walls = sm64_surface_find_wall_collision(&x, &y, &z, 60.0f, 50.0f);
	CHECK(walls > 0);
	CHECK(f.toGta({x, y, z}).x < 4.9f);

	// a car facing west (heading 90) at (-6, -6): 5 m long along GTA x after the turn, 2 m wide along y
	const std::vector<Surface> box = vehicleBox({-1, -2.5f, 0}, {1, 2.5f, 1.5f}, f.scale);
	SM64SurfaceObject obj{};
	const V3 at = f.toSm({-6, -6, 0});
	obj.transform.position[0] = at.x;
	obj.transform.position[1] = at.y;
	obj.transform.position[2] = at.z;
	obj.transform.eulerRotation[1] = smObjectYawFromGtaHeading(90.0f);
	obj.surfaceCount = static_cast<uint32_t>(box.size());
	std::vector<Surface> boxCopy = box;
	obj.surfaces = reinterpret_cast<SM64Surface *>(boxCopy.data());
	const uint32_t id = sm64_surface_object_create(&obj);
	CHECK(near(floorAt(-6, -6, 3), 1.5f, 0.05f));
	CHECK(near(floorAt(-8, -6, 3), 1.5f, 0.05f));  // 2 m along the car's length
	CHECK(near(floorAt(-6, -8, 3), 0.0f, 0.05f));  // 2 m to its side: off the roof
	// and moving it moves its roof
	SM64ObjectTransform moved = obj.transform;
	const V3 at2 = f.toSm({-6, 6, 0});
	moved.position[0] = at2.x;
	moved.position[2] = at2.z;
	sm64_surface_object_move(id, &moved);
	CHECK(near(floorAt(-6, 6, 3), 1.5f, 0.05f));
	CHECK(near(floorAt(-6, -6, 3), 0.0f, 0.05f));
	sm64_surface_object_delete(id);
	// a box that only reaches forward of its origin (local +y, the car's nose), at heading 45: GTA turns local +y
	// counter-clockwise to (-sin 45, cos 45), north-west. A wrong sign would put it north-east instead.
	const std::vector<Surface> nose = vehicleBox({-0.5f, 0, 0}, {0.5f, 4, 1.5f}, f.scale);
	SM64SurfaceObject obj2{};
	const V3 at3 = f.toSm({-6, -6, 0});
	obj2.transform.position[0] = at3.x;
	obj2.transform.position[1] = at3.y;
	obj2.transform.position[2] = at3.z;
	obj2.transform.eulerRotation[1] = smObjectYawFromGtaHeading(45.0f);
	obj2.surfaceCount = static_cast<uint32_t>(nose.size());
	std::vector<Surface> noseCopy = nose;
	obj2.surfaces = reinterpret_cast<SM64Surface *>(noseCopy.data());
	const uint32_t id2 = sm64_surface_object_create(&obj2);
	const float d = 2.0f * 0.7071f;
	CHECK(near(floorAt(-6 - d, -6 + d, 3), 1.5f, 0.05f)); // north-west: on it
	CHECK(near(floorAt(-6 + d, -6 + d, 3), 0.0f, 0.05f)); // north-east: not
	sm64_surface_object_delete(id2);
	std::printf("libsm64: %zu static surfaces checked\n", s.size());
}
#endif

int main()
{
	testFrame();
	testCollision();
	testCamera();
	testCombat();
	testRomAndTick();
#ifdef WITH_LIBSM64
	testLibsm64();
#endif
	std::printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
