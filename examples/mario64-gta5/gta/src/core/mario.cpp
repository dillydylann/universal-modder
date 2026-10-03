#include "mario.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace m64
{
RomResult normalizeRom(std::vector<uint8_t> &rom)
{
	if (rom.size() != 8u * 1024 * 1024)
		return RomResult::WrongSize;
	const uint8_t *h = rom.data();
	if (h[0] == 0x37 && h[1] == 0x80 && h[2] == 0x40 && h[3] == 0x12) // .v64: 16-bit words swapped
		for (size_t i = 0; i + 1 < rom.size(); i += 2)
			std::swap(rom[i], rom[i + 1]);
	else if (h[0] == 0x40 && h[1] == 0x12 && h[2] == 0x37 && h[3] == 0x80) // .n64: 32-bit little-endian
		for (size_t i = 0; i + 3 < rom.size(); i += 4)
		{
			std::swap(rom[i], rom[i + 3]);
			std::swap(rom[i + 1], rom[i + 2]);
		}
	if (!(h[0] == 0x80 && h[1] == 0x37 && h[2] == 0x12 && h[3] == 0x40))
		return RomResult::NotSm64;
	if (std::memcmp(h + 0x20, "SUPER MARIO 64", 14) != 0)
		return RomResult::NotSm64;
	if (h[0x3E] != 'E')
		return RomResult::NotUs;
	return RomResult::Ok;
}

const char *romResultText(RomResult r)
{
	switch (r)
	{
	case RomResult::Ok: return "ok";
	case RomResult::WrongSize: return "the ROM should be 8 MiB";
	case RomResult::NotSm64: return "that isn't a Super Mario 64 ROM";
	case RomResult::NotUs: return "that's Super Mario 64, but libsm64 needs the USA version";
	}
	return "?";
}

int FixedStep::advance(float dt)
{
	if (dt < 0)
		dt = 0;
	acc += dt;
	int n = 0;
	while (acc >= step && n < maxSteps)
	{
		acc -= step;
		++n;
	}
	if (acc >= step) // too far behind: drop the backlog rather than spiral
		acc = std::fmod(acc, step);
	return n;
}

Rgb triangleColor(const float *c, const float *uv, const uint8_t *atlas, const V3 &n, const V3 &light, float ambient)
{
	float r = (c[0] + c[3] + c[6]) / 3, g = (c[1] + c[4] + c[7]) / 3, b = (c[2] + c[5] + c[8]) / 3;
	const float u = (uv[0] + uv[2] + uv[4]) / 3, v = (uv[1] + uv[3] + uv[5]) / 3;
	if (atlas && u >= 0 && u < 1 && v >= 0 && v < 1)
	{
		const int x = static_cast<int>(u * kAtlasWidth), y = static_cast<int>(v * kAtlasHeight);
		const uint8_t *t = atlas + 4 * (y * kAtlasWidth + x);
		const float a = t[3] / 255.0f;
		r += (t[0] / 255.0f - r) * a;
		g += (t[1] / 255.0f - g) * a;
		b += (t[2] / 255.0f - b) * a;
	}
	const float d = dot(normalize(n), normalize(light));
	const float lit = clampf(ambient + (1 - ambient) * (d > 0 ? d : 0), 0, 1);
	auto byte = [lit](float x) { return static_cast<uint8_t>(clampf(x * lit, 0, 1) * 255.0f + 0.5f); };
	return {byte(r), byte(g), byte(b)};
}
} // namespace m64
