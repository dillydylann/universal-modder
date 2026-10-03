// Collision for Mario, built from GTA's world with ray probes.
//
// GTA has no native that hands out its collision mesh, so the script probes the world around Mario and turns
// the hits into SM64 triangles:
//  - floor: a downward probe per cell on a grid round Mario, from high above; each hit becomes a quad on the
//    plane of the hit (its point and normal). When that probe lands on something well above Mario (a roof, a
//    bridge, an awning), a second probe from just above Mario finds what's under it, and an upward probe from
//    there tells the two apart: if it hits an underside, it's an overhang and Mario walks under it; if it
//    doesn't (it started inside a solid building, and probes don't see shapes from inside), the roof is the
//    floor and the cell's sides become walls;
//  - steps: where two neighbouring floor quads meet at different heights, a vertical wall joins them (curbs,
//    ledges, roofs seen from below);
//  - walls: horizontal probes fanned out round Mario at a few heights; each hit on a near-vertical face becomes
//    a wall quad facing back along the hit normal.
// Probes are spread over frames (a budget per frame); when a whole window is done it replaces the old one.
// Vehicles move, so they aren't probed: the script gives each nearby vehicle a box (see vehicleBox).
#pragma once
#include "geom.h"
#include <functional>
#include <vector>

namespace m64
{
struct ProbeHit
{
	bool hit = false;
	V3 pos, normal;
};
// a line-of-sight probe in GTA coordinates
using Probe = std::function<ProbeHit(const V3 &from, const V3 &to)>;

// a triangle in GTA coordinates with the side that should face Mario (its normal)
struct Tri
{
	V3 a, b, c;
	V3 n;
};

// the same layout as libsm64's SM64Surface
struct Surface
{
	int16_t type;
	int16_t force;
	uint16_t terrain;
	int32_t v[3][3];
};

enum : uint16_t
{
	TERRAIN_GRASS = 0,
	TERRAIN_STONE = 1,
};

struct CollisionConfig
{
	float cell = 1.0f;           // grid cell size, metres
	int radiusCells = 12;        // the grid is (2r) x (2r) cells round the centre
	float high = 12.0f;          // floor probes start this far above the reference height...
	float depth = 40.0f;         // ...and go this far below it
	float headroom = 2.0f;       // a floor hit higher than this above the reference gets the overhang test
	float stepWall = 0.45f;      // neighbouring floors further apart than this get a wall between them
	float maxFloorTilt = 0.35f;  // a floor hit with normal.z below this is treated as flat (it hit a wall edge)
	int wallDirs = 24;           // horizontal probes per height
	float wallRange = 8.0f;
	float wallHeights[3] = {0.3f, 1.1f, 2.0f};
	float wallHalfWidth = 0.9f;
	float wallHalfHeight = 0.6f;
	float wallMaxNormalZ = 0.5f; // a horizontal hit steeper than this is a wall
};

class CollisionBuilder
{
public:
	explicit CollisionBuilder(const CollisionConfig &cfg = {}) : cfg_(cfg) {}

	// plans a new window round `center` (GTA coords; center.z is the reference height, i.e. Mario's feet)
	void begin(const V3 &center);
	// runs up to `budget` probes; true once the window is complete (then take tris())
	bool step(const Probe &probe, int budget);
	bool busy() const { return busy_; }
	int pending() const { return static_cast<int>(probes_.size() - next_); }
	const V3 &center() const { return center_; }
	const std::vector<Tri> &tris() const { return tris_; }
	const CollisionConfig &config() const { return cfg_; }

private:
	struct Cell
	{
		bool hit = false;
		V3 p, n;
		V3 highP, lowP, lowN; // the high and low probes' hits while the overhang test runs
		float heightAt(float x, float y) const; // the cell's plane
	};
	enum class Kind
	{
		High, // from high above: the floor, unless it's well above Mario
		Low,  // from just above Mario, under something high
		Up,   // from the low hit up to the high one: an overhang if it hits
		Wall,
	};
	struct Job
	{
		Kind kind;
		int index; // the cell; unused for walls
		V3 from, to;
	};
	void finish();

	CollisionConfig cfg_;
	V3 center_;
	float x0_ = 0, y0_ = 0; // the grid's corner
	int n_ = 0;             // cells per side
	std::vector<Cell> cells_;
	std::vector<Job> probes_;
	std::vector<ProbeHit> wallHits_;
	size_t next_ = 0;
	bool busy_ = false;
	std::vector<Tri> tris_;
};

// SM64 triangles from GTA triangles: vertices to the frame, winding fixed so SM64's normal ((v2-v1) x (v3-v2))
// points along the triangle's intended normal. Degenerate triangles (after rounding to whole units) are dropped.
std::vector<Surface> toSurfaces(const std::vector<Tri> &tris, const Frame &frame, uint16_t terrain = TERRAIN_STONE);

// A box in SM64 units for a vehicle, in the vehicle's own frame (libsm64 surface objects are placed with a
// transform each tick). min/max are GET_MODEL_DIMENSIONS's (metres; x right, y forward, z up).
std::vector<Surface> vehicleBox(const V3 &min, const V3 &max, float scale);
// libsm64's eulerRotation[1] (degrees) for a GTA heading (degrees). Checked against libsm64 in tests.
float smObjectYawFromGtaHeading(float headingDeg);
} // namespace m64
