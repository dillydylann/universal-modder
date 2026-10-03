// A Mario 64 style camera ("Mario cam") for GTA's scripted camera.
//
// What makes it feel like SM64 rather than GTA's own follow cam:
//  - it trails Mario at a fixed distance and only swings round behind him while he runs, at a rate that grows
//    with his speed, and not at all when he runs straight at the camera (it backs up instead, like Lakitu);
//  - it doesn't bob up for every jump: the focus height follows the ground Mario last stood on, and only follows
//    him in the air once he's well above it or falling below it;
//  - three zoom distances (C-up / C-down), a recenter-behind button (the R button's Mario-cam snap) and free
//    orbiting with the mouse or right stick, after which it eases back to auto-follow;
//  - it pulls in against walls rather than clipping through them.
#pragma once
#include "collision.h"
#include "geom.h"

namespace m64
{
struct CamConfig
{
	float distances[3] = {4.0f, 6.5f, 9.5f}; // metres, near / normal / far
	float focusHeight = 1.1f;                // look at this far above Mario's feet
	float defaultPitchDeg = 16.0f;           // looking down
	float minPitchDeg = -20.0f, maxPitchDeg = 70.0f;
	float followRate = 1.8f;                 // radians per second of auto swing at full run speed
	float fullSpeed = 9.6f;                  // m/s: Mario's top running speed at 100 units per metre
	float idleRecenterSec = 1.2f;            // after the player orbits by hand, wait this long to auto-follow
	float pitchReturnRate = 1.5f;            // pitch eases back to the default this fast (per second)
	float airFollowAbove = 2.5f;             // metres above the last ground before the focus follows a jump
	float smoothing = 10.0f;                 // position smoothing (1/s)
	float zSmoothing = 4.0f;                 // vertical focus smoothing (1/s)
	float wallPad = 0.3f;
	float minDistance = 0.8f;
	float fovDeg = 55.0f;
};

struct CamInput
{
	// radians to orbit by this frame: right, and down. The script scales the mouse (a per-frame delta) and the
	// stick (a rate, times the frame time) differently, so the camera doesn't have to know which it was.
	float turn = 0, tilt = 0;
	int zoom = 0;               // -1 in, +1 out, this frame
	bool recenter = false;
};

struct CamPose
{
	V3 pos;
	float pitchDeg = 0;   // GTA rotation x (negative looks down)
	float headingDeg = 0; // GTA rotation z
	float fovDeg = 55;
};

class MarioCam
{
public:
	explicit MarioCam(const CamConfig &cfg = {}) : cfg_(cfg) {}
	void reset(const V3 &marioFeet, float marioHeadingRad);
	// marioHeadingRad: GTA heading of Mario's facing; speed: horizontal m/s; onGround: not in an air action
	CamPose update(float dt, const V3 &marioFeet, float marioHeadingRad, float speed, bool onGround,
		const CamInput &in, const Probe *probe);

	float yaw() const { return yaw_; } // GTA heading the camera looks along, radians
	int zoom() const { return zoom_; }
	const V3 &position() const { return pos_; } // where the camera was last put (GTA coords)
	const CamConfig &config() const { return cfg_; }

private:
	CamConfig cfg_;
	float yaw_ = 0, pitch_ = 0; // pitch in radians, positive looks down
	int zoom_ = 1;
	float sinceManual_ = 1e9f;
	bool recentering_ = false;
	float groundZ_ = 0, focusZ_ = 0;
	V3 pos_;
	bool init_ = false;
};
} // namespace m64
