#include "camera.h"
#include <cmath>

namespace m64
{
static V3 lookDir(float yaw, float pitchDown)
{
	return {-std::sin(yaw) * std::cos(pitchDown), std::cos(yaw) * std::cos(pitchDown), -std::sin(pitchDown)};
}

static float approach(float cur, float target, float maxStep)
{
	const float d = wrapAngle(target - cur);
	return wrapAngle(cur + clampf(d, -maxStep, maxStep));
}

void MarioCam::reset(const V3 &feet, float heading)
{
	yaw_ = heading;
	pitch_ = cfg_.defaultPitchDeg * kPi / 180.0f;
	groundZ_ = focusZ_ = feet.z;
	const V3 focus{feet.x, feet.y, feet.z + cfg_.focusHeight};
	pos_ = focus - lookDir(yaw_, pitch_) * cfg_.distances[zoom_];
	sinceManual_ = 1e9f;
	recentering_ = false;
	init_ = true;
}

CamPose MarioCam::update(float dt, const V3 &feet, float heading, float speed, bool onGround, const CamInput &in,
	const Probe *probe)
{
	if (!init_)
		reset(feet, heading);
	if (dt <= 0)
		dt = 1.0f / 60;
	zoom_ = static_cast<int>(clampf(static_cast<float>(zoom_ + in.zoom), 0, 2));

	const float minP = cfg_.minPitchDeg * kPi / 180.0f, maxP = cfg_.maxPitchDeg * kPi / 180.0f;
	const float defP = cfg_.defaultPitchDeg * kPi / 180.0f;
	if (std::fabs(in.turn) > 1e-4f || std::fabs(in.tilt) > 1e-4f)
	{
		yaw_ = wrapAngle(yaw_ - in.turn);
		pitch_ = clampf(pitch_ + in.tilt, minP, maxP);
		sinceManual_ = 0;
		recentering_ = false;
	}
	else
		sinceManual_ += dt;
	if (in.recenter)
		recentering_ = true;

	if (recentering_)
	{
		yaw_ = approach(yaw_, heading, 10.0f * dt);
		pitch_ += (defP - pitch_) * clampf(10.0f * dt, 0, 1);
		if (std::fabs(wrapAngle(heading - yaw_)) < 0.01f)
			recentering_ = false;
	}
	else if (sinceManual_ > cfg_.idleRecenterSec)
	{
		if (speed > 0.3f)
		{
			const float diff = wrapAngle(heading - yaw_);
			// full swing while Mario runs across or away from the camera, none when he runs straight at it
			const float facing = clampf((kPi - std::fabs(diff)) / (kPi / 2), 0, 1);
			const float rate = cfg_.followRate * clampf(speed / cfg_.fullSpeed, 0, 1) * facing;
			yaw_ = approach(yaw_, heading, rate * dt);
		}
		const float dp = defP - pitch_, maxStep = cfg_.pitchReturnRate * dt;
		pitch_ += clampf(dp, -maxStep, maxStep);
	}

	// the focus height: the ground Mario last stood on, unless he's far above it or below it
	if (onGround || feet.z < groundZ_)
		groundZ_ = feet.z;
	else if (feet.z > groundZ_ + cfg_.airFollowAbove)
		groundZ_ = feet.z - cfg_.airFollowAbove;
	focusZ_ += (groundZ_ - focusZ_) * clampf(1.0f - std::exp(-cfg_.zSmoothing * dt), 0, 1);
	const V3 focus{feet.x, feet.y, focusZ_ + cfg_.focusHeight};

	const V3 dir = lookDir(yaw_, pitch_);
	float dist = cfg_.distances[zoom_];
	bool blocked = false;
	if (probe)
	{
		const ProbeHit h = (*probe)(focus, focus - dir * dist);
		if (h.hit)
		{
			const float d = length(h.pos - focus) - cfg_.wallPad;
			dist = d > cfg_.minDistance ? d : cfg_.minDistance;
			blocked = true;
		}
	}
	const V3 desired = focus - dir * dist;
	pos_ = blocked ? desired : lerp(pos_, desired, clampf(1.0f - std::exp(-cfg_.smoothing * dt), 0, 1));

	CamPose pose;
	pose.pos = pos_;
	const V3 look = focus - pos_;
	pose.headingDeg = headingOf(look) * 180.0f / kPi;
	pose.pitchDeg = std::atan2(look.z, length2d(look)) * 180.0f / kPi;
	pose.fovDeg = cfg_.fovDeg;
	return pose;
}
} // namespace m64
