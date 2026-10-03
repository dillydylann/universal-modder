// Vectors and the mapping between GTA's world and libsm64's.
// No Windows, ScriptHookV or libsm64 here: everything in core/ builds and is tested on any OS (tests/core_test.cpp).
#pragma once
#include <cmath>
#include <cstdint>

namespace m64
{
constexpr float kPi = 3.14159265358979f;

struct V3
{
	float x = 0, y = 0, z = 0;
	V3() = default;
	V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
	V3 operator+(const V3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
	V3 operator-(const V3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
	V3 operator*(float s) const { return {x * s, y * s, z * s}; }
	V3 operator-() const { return {-x, -y, -z}; }
};
inline float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(const V3 &a, const V3 &b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(const V3 &a) { return std::sqrt(dot(a, a)); }
inline float length2d(const V3 &a) { return std::sqrt(a.x * a.x + a.y * a.y); }
inline V3 normalize(const V3 &a)
{
	const float l = length(a);
	return l > 1e-6f ? a * (1.0f / l) : V3{0, 0, 0};
}
inline V3 lerp(const V3 &a, const V3 &b, float t) { return a + (b - a) * t; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
// wraps an angle in radians to (-pi, pi]
inline float wrapAngle(float a)
{
	while (a > kPi) a -= 2 * kPi;
	while (a <= -kPi) a += 2 * kPi;
	return a;
}

// GTA: metres, Z up, heading in degrees counter-clockwise from +Y (north).
// SM64: units, Y up. Mario is about 160 units tall, so the default 100 units per metre makes him 1.6 m.
//   sm = ((g - origin).x, (g - origin).z, -(g - origin).y) * scale
// That is a proper rotation (no mirror), so triangle winding and handedness survive the trip.
// The origin floats: it is moved next to Mario now and then so libsm64's numbers stay small (Los Santos is
// 8 km across, 800,000 units at this scale).
struct Frame
{
	V3 origin;
	float scale = 100.0f;

	V3 toSm(const V3 &g) const
	{
		const V3 d = g - origin;
		return {d.x * scale, d.z * scale, -d.y * scale};
	}
	V3 toGta(const V3 &s) const
	{
		const float k = 1.0f / scale;
		return origin + V3{s.x * k, -s.z * k, s.y * k};
	}
	// directions and normals: no origin, no scale
	static V3 dirToSm(const V3 &g) { return {g.x, g.z, -g.y}; }
	static V3 dirToGta(const V3 &s) { return {s.x, -s.z, s.y}; }
};

// Mario's faceAngle (radians, 0 = facing SM64 +Z) <-> GTA heading (radians, 0 = facing +Y).
// SM64 forward is (sin a, 0, cos a), which is GTA (sin a, -cos a, 0); GTA forward is (-sin h, cos h, 0), so h = a + pi.
inline float gtaHeadingFromSmYaw(float a) { return wrapAngle(a + kPi); }
inline float smYawFromGtaHeading(float h) { return wrapAngle(h - kPi); }
inline V3 gtaForward(float headingRad) { return {-std::sin(headingRad), std::cos(headingRad), 0}; }
inline float headingOf(const V3 &dir) { return std::atan2(-dir.x, dir.y); }
} // namespace m64
