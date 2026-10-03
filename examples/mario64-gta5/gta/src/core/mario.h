// Small pieces between libsm64 and GTA: the user's ROM, the 30 Hz tick, Mario's triangle colours.
#pragma once
#include "geom.h"
#include <cstdint>
#include <vector>

namespace m64
{
// libsm64 reads Mario's model, textures and animations from the player's own Super Mario 64 (USA) ROM.
// Dumps come in three byte orders; this puts any of them in .z64 (big-endian) order and checks the header.
enum class RomResult
{
	Ok,
	WrongSize, // not an 8 MiB cartridge
	NotSm64,   // unknown byte order or another game
	NotUs,     // Super Mario 64, but not the USA version libsm64 is built for
};
RomResult normalizeRom(std::vector<uint8_t> &rom);
const char *romResultText(RomResult r);

// SM64 runs at 30 ticks a second; GTA at whatever it gets. The script ticks Mario at 30 Hz and draws him
// interpolated between the last two ticks.
struct FixedStep
{
	float step = 1.0f / 30.0f;
	int maxSteps = 4; // after a hitch, catch up at most this many ticks (then drop the rest)
	float acc = 0;
	int advance(float dt);
	float alpha() const { return acc / step; }
};

// libsm64 hands out Mario as triangles with per-vertex colours (0..1) and UVs into a 704x64 RGBA atlas (the
// eyes, buttons, sideburns, cap emblem). GTA's DRAW_POLY takes one colour per triangle, so: the vertex colour
// average, mixed with the atlas at the triangle's centre by the atlas's alpha (as libsm64's own test shader
// does per pixel), lit by a sun direction (GTA coords) with some ambient.
struct Rgb
{
	uint8_t r, g, b;
};
constexpr int kAtlasWidth = 64 * 11, kAtlasHeight = 64;
Rgb triangleColor(const float *colors9, const float *uvs6, const uint8_t *atlasRgba, const V3 &normalGta,
	const V3 &lightDirGta, float ambient);
} // namespace m64
