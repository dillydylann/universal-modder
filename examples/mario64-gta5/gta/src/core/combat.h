// Mario vs GTA's people, both ways.
//
// Mario -> peds: each tick, for every ped in reach, libsm64 decides whether Mario's current move connects
// (sm64_mario_attack: punches and kicks within 60 degrees of where he faces, dives, slide kicks, stomps from
// above, a falling ground pound). It checks angles, not distance, so the reach test is ours. On top of that, a
// ground pound that lands sends out a shockwave: everyone within a few metres takes damage that falls off with
// distance, and may die on the spot.
//
// Peds -> Mario: GTA damages the (hidden) player ped that stands in for Mario. The script refills its health
// every frame and passes the difference to Mario as wedges of his power meter (sm64_mario_take_damage).
#pragma once
#include "geom.h"
#include <cstdint>
#include <functional>
#include <random>
#include <unordered_map>
#include <vector>

namespace m64
{
// SM64 action ids and flags (the decomp's include/sm64.h)
enum : uint32_t
{
	ACT_FLAG_AIR = 1u << 11,
	ACT_FLAG_INTANGIBLE = 1u << 12,
	ACT_FLAG_INVULNERABLE = 1u << 17,
	ACT_FLAG_ATTACKING = 1u << 23,
	ACT_IDLE = 0x0C400201,
	ACT_PUNCHING = 0x00800380,
	ACT_MOVE_PUNCHING = 0x00800457,
	ACT_JUMP_KICK = 0x018008AC,
	ACT_DIVE = 0x0188088A,
	ACT_DIVE_SLIDE = 0x00880456,
	ACT_SLIDE_KICK = 0x018008AA,
	ACT_SLIDE_KICK_SLIDE = 0x0080045A,
	ACT_GROUND_POUND = 0x008008A9,
	ACT_GROUND_POUND_LAND = 0x0080023C,
	ACT_STANDING_DEATH = 0x00021311,
};
inline bool isAirAction(uint32_t a) { return (a & ACT_FLAG_AIR) != 0; }
inline bool isAttackAction(uint32_t a) { return (a & ACT_FLAG_ATTACKING) != 0; }

enum class Move
{
	None,
	Punch,
	Kick,
	Dive,
	SlideKick,
	Stomp,       // landing on someone from a jump
	GroundPound, // falling onto someone in a ground pound
	Shockwave,   // the ground pound landing next to someone
};
const char *moveName(Move m);
// what move a connecting attack was, from Mario's action and vertical speed (SM64 units/frame)
Move classify(uint32_t action, float velY);

struct CombatConfig
{
	float reach = 1.4f;           // metres between Mario and a ped's centre, horizontally
	float reachBelow = 1.6f;      // a ped's centre may be this far below Mario's feet (stomps)...
	float reachAbove = 1.9f;      // ...or this far above them (punches)
	float pedHitHeight = 0.6f;    // the point handed to libsm64 is this far above the ped's centre
	int damage[8] = {0, 25, 35, 30, 40, 35, 100, 60}; // by Move
	float groundPoundKillChance = 0.5f; // falling straight onto someone
	float shockRadius = 3.5f;
	float shockKillChance = 0.3f; // at the centre; falls to 0 at the edge
	float knockback = 6.0f;       // impulse, scaled by damage
	int cooldownTicks = 8;        // one ped can't be hit again for this many ticks (30 per second)
};

struct PedInfo
{
	int id = 0;
	V3 pos; // GTA coords of the ped's centre (GET_ENTITY_COORDS)
};

struct PedHit
{
	int id = 0;
	Move move = Move::None;
	int damage = 0;
	bool kill = false;
	V3 push; // impulse direction * strength, GTA coords
};

class Combat
{
public:
	explicit Combat(const CombatConfig &cfg = {}, uint32_t seed = 0x5EED64u) : cfg_(cfg), rng_(seed) {}

	// One libsm64 tick. marioFeet in GTA coords; velY in SM64 units/frame. attack(ped) wraps
	// sm64_mario_attack for that ped (and may make Mario bounce). Returns what happened to whom.
	std::vector<PedHit> tick(uint32_t action, float velY, const V3 &marioFeet, float marioHeadingRad,
		const std::vector<PedInfo> &peds, const std::function<bool(const PedInfo &)> &attack);
	const CombatConfig &config() const { return cfg_; }
	int ticks() const { return tick_; }

private:
	bool inReach(const V3 &feet, const PedInfo &p) const;
	bool cooling(int id) const;
	bool roll(float chance);

	CombatConfig cfg_;
	std::mt19937 rng_;
	std::unordered_map<int, int> lastHit_;
	uint32_t prevAction_ = 0;
	int tick_ = 0;
};

// GTA health lost by the player ped -> wedges off Mario's power meter (he has 8). 0 for nothing worth a wedge.
int wedgesForGtaDamage(int hp, int hpPerWedge = 20, int maxWedges = 3);
} // namespace m64
