#include "combat.h"
#include <cmath>

namespace m64
{
const char *moveName(Move m)
{
	switch (m)
	{
	case Move::Punch: return "punch";
	case Move::Kick: return "kick";
	case Move::Dive: return "dive";
	case Move::SlideKick: return "slide kick";
	case Move::Stomp: return "stomp";
	case Move::GroundPound: return "ground pound";
	case Move::Shockwave: return "shockwave";
	default: return "none";
	}
}

Move classify(uint32_t action, float velY)
{
	switch (action)
	{
	case ACT_GROUND_POUND:
	case ACT_GROUND_POUND_LAND: return Move::GroundPound;
	case ACT_SLIDE_KICK:
	case ACT_SLIDE_KICK_SLIDE: return Move::SlideKick;
	case ACT_DIVE:
	case ACT_DIVE_SLIDE: return Move::Dive;
	case ACT_JUMP_KICK: return Move::Kick;
	case ACT_PUNCHING:
	case ACT_MOVE_PUNCHING: return Move::Punch;
	default: break;
	}
	if (isAirAction(action) && velY < 0)
		return Move::Stomp;
	if (isAttackAction(action))
		return Move::Punch; // other attacking actions: fast running bumps, belly slides
	return Move::None;
}

bool Combat::inReach(const V3 &feet, const PedInfo &p) const
{
	const float dz = p.pos.z - feet.z;
	return length2d(p.pos - feet) <= cfg_.reach && dz >= -cfg_.reachBelow && dz <= cfg_.reachAbove;
}

bool Combat::cooling(int id) const
{
	const auto it = lastHit_.find(id);
	return it != lastHit_.end() && tick_ - it->second < cfg_.cooldownTicks;
}

bool Combat::roll(float chance)
{
	return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng_) < chance;
}

static V3 pushFrom(const V3 &from, const V3 &to, float heading, float strength)
{
	V3 d{to.x - from.x, to.y - from.y, 0};
	d = length2d(d) > 0.05f ? normalize(d) : gtaForward(heading);
	return V3{d.x, d.y, 0.45f} * strength;
}

std::vector<PedHit> Combat::tick(uint32_t action, float velY, const V3 &feet, float heading,
	const std::vector<PedInfo> &peds, const std::function<bool(const PedInfo &)> &attack)
{
	++tick_;
	std::vector<PedHit> hits;
	const bool canHit = isAttackAction(action) || (isAirAction(action) && velY < 0);
	if (canHit)
		for (const PedInfo &p : peds)
		{
			if (!inReach(feet, p) || cooling(p.id) || !attack(p))
				continue;
			const Move m = classify(action, velY);
			if (m == Move::None)
				continue;
			PedHit h;
			h.id = p.id;
			h.move = m;
			h.damage = cfg_.damage[static_cast<int>(m)];
			h.kill = m == Move::GroundPound && roll(cfg_.groundPoundKillChance);
			h.push = pushFrom(feet, p.pos, heading, cfg_.knockback * h.damage / 25.0f);
			hits.push_back(h);
			lastHit_[p.id] = tick_;
		}

	// the moment a ground pound lands: a shockwave round Mario
	if (action == ACT_GROUND_POUND_LAND && prevAction_ != ACT_GROUND_POUND_LAND)
		for (const PedInfo &p : peds)
		{
			const float d = length2d(p.pos - feet), dz = p.pos.z - feet.z;
			if (d > cfg_.shockRadius || dz < -1.0f || dz > 2.0f || cooling(p.id))
				continue;
			const float f = 1.0f - d / cfg_.shockRadius;
			PedHit h;
			h.id = p.id;
			h.move = Move::Shockwave;
			h.damage = static_cast<int>(std::lround(cfg_.damage[static_cast<int>(Move::Shockwave)] * (0.5f + 0.5f * f)));
			h.kill = roll(cfg_.shockKillChance * f);
			h.push = pushFrom(feet, p.pos, heading, cfg_.knockback * (1.0f + f));
			hits.push_back(h);
			lastHit_[p.id] = tick_;
		}
	prevAction_ = action;
	return hits;
}

int wedgesForGtaDamage(int hp, int hpPerWedge, int maxWedges)
{
	if (hp <= 0 || hpPerWedge <= 0)
		return 0;
	const int w = (hp + hpPerWedge - 1) / hpPerWedge;
	return w > maxWedges ? maxWedges : w;
}
} // namespace m64
