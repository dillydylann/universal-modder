#include "collision.h"
#include <algorithm>
#include <cmath>

namespace m64
{
float CollisionBuilder::Cell::heightAt(float x, float y) const
{
	return p.z - (n.x * (x - p.x) + n.y * (y - p.y)) / n.z;
}

void CollisionBuilder::begin(const V3 &center)
{
	center_ = center;
	n_ = cfg_.radiusCells * 2;
	x0_ = std::floor(center.x / cfg_.cell) * cfg_.cell - cfg_.radiusCells * cfg_.cell;
	y0_ = std::floor(center.y / cfg_.cell) * cfg_.cell - cfg_.radiusCells * cfg_.cell;
	cells_.assign(static_cast<size_t>(n_) * n_, Cell{});
	probes_.clear();
	wallHits_.clear();
	next_ = 0;

	const float top = center.z + cfg_.high, bottom = center.z - cfg_.depth;
	// nearest cells first, so a half-done window is still useful while debugging
	std::vector<int> order(cells_.size());
	for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
	auto cellCenter = [&](int idx) {
		return V3{x0_ + (idx % n_ + 0.5f) * cfg_.cell, y0_ + (idx / n_ + 0.5f) * cfg_.cell, 0};
	};
	std::sort(order.begin(), order.end(), [&](int a, int b) {
		return length2d(cellCenter(a) - center) < length2d(cellCenter(b) - center);
	});
	for (int idx : order)
	{
		const V3 c = cellCenter(idx);
		probes_.push_back({Kind::High, idx, {c.x, c.y, top}, {c.x, c.y, bottom}});
	}
	for (float h : cfg_.wallHeights)
		for (int d = 0; d < cfg_.wallDirs; ++d)
		{
			const float a = 2 * kPi * d / cfg_.wallDirs;
			const V3 from{center.x, center.y, center.z + h};
			probes_.push_back({Kind::Wall, -1, from, from + V3{std::cos(a), std::sin(a), 0} * cfg_.wallRange});
		}
	busy_ = true;
}

bool CollisionBuilder::step(const Probe &probe, int budget)
{
	if (!busy_)
		return false;
	auto floorNormal = [&](const V3 &n) { return n.z >= cfg_.maxFloorTilt ? normalize(n) : V3{0, 0, 1}; };
	for (; budget > 0 && next_ < probes_.size(); --budget, ++next_)
	{
		const Job j = probes_[next_]; // a copy: follow-up probes are appended to probes_
		const ProbeHit h = probe(j.from, j.to);
		if (j.kind == Kind::Wall)
		{
			if (h.hit)
				wallHits_.push_back(h);
			continue;
		}
		Cell &c = cells_[j.index];
		const float lowTop = center_.z + cfg_.headroom;
		switch (j.kind)
		{
		case Kind::High:
			if (!h.hit)
				break;
			c.hit = true;
			c.p = h.pos;
			c.n = floorNormal(h.normal);
			if (h.pos.z > lowTop + 0.05f)
			{
				c.highP = c.p;
				probes_.push_back({Kind::Low, j.index, {j.from.x, j.from.y, lowTop}, j.to});
			}
			break;
		case Kind::Low:
			// no hit under it: keep the high floor
			if (h.hit)
			{
				c.lowP = h.pos;
				c.lowN = floorNormal(h.normal);
				probes_.push_back({Kind::Up, j.index, h.pos + V3{0, 0, 0.05f}, c.highP - V3{0, 0, 0.05f}});
			}
			break;
		case Kind::Up:
			if (h.hit)
			{
				// an underside above Mario: an overhang, so he walks under it on what the low probe found
				c.p = c.lowP;
				c.n = c.lowN;
			}
			break;
		default:
			break;
		}
	}
	if (next_ < probes_.size())
		return false;
	finish();
	busy_ = false;
	return true;
}

void CollisionBuilder::finish()
{
	tris_.clear();
	const float s = cfg_.cell;
	auto quad = [&](const V3 &a, const V3 &b, const V3 &c, const V3 &d, const V3 &n) {
		tris_.push_back({a, b, c, n});
		tris_.push_back({a, c, d, n});
	};
	// floors
	for (int j = 0; j < n_; ++j)
		for (int i = 0; i < n_; ++i)
		{
			const Cell &c = cells_[j * n_ + i];
			if (!c.hit)
				continue;
			const float xa = x0_ + i * s, xb = xa + s, ya = y0_ + j * s, yb = ya + s;
			quad({xa, ya, c.heightAt(xa, ya)}, {xb, ya, c.heightAt(xb, ya)}, {xb, yb, c.heightAt(xb, yb)},
				{xa, yb, c.heightAt(xa, yb)}, c.n);
		}
	// steps between neighbouring floors; the wall faces the lower floor
	auto step = [&](const Cell &a, const Cell &b, const V3 &e0, const V3 &e1, const V3 &towardB) {
		if (!a.hit || !b.hit)
			return;
		const float a0 = a.heightAt(e0.x, e0.y), a1 = a.heightAt(e1.x, e1.y);
		const float b0 = b.heightAt(e0.x, e0.y), b1 = b.heightAt(e1.x, e1.y);
		if (std::max(std::fabs(a0 - b0), std::fabs(a1 - b1)) <= cfg_.stepWall)
			return;
		const V3 n = (a0 + a1) > (b0 + b1) ? towardB : -towardB;
		quad({e0.x, e0.y, std::min(a0, b0)}, {e1.x, e1.y, std::min(a1, b1)}, {e1.x, e1.y, std::max(a1, b1)},
			{e0.x, e0.y, std::max(a0, b0)}, n);
	};
	for (int j = 0; j < n_; ++j)
		for (int i = 0; i < n_; ++i)
		{
			const Cell &c = cells_[j * n_ + i];
			const float xa = x0_ + i * s, ya = y0_ + j * s;
			if (i + 1 < n_)
				step(c, cells_[j * n_ + i + 1], {xa + s, ya, 0}, {xa + s, ya + s, 0}, {1, 0, 0});
			if (j + 1 < n_)
				step(c, cells_[(j + 1) * n_ + i], {xa, ya + s, 0}, {xa + s, ya + s, 0}, {0, 1, 0});
		}
	// walls seen from the centre
	for (const ProbeHit &h : wallHits_)
	{
		if (std::fabs(h.normal.z) > cfg_.wallMaxNormalZ)
			continue;
		const V3 n = normalize({h.normal.x, h.normal.y, 0});
		const V3 t{-n.y, n.x, 0};
		const V3 w = t * cfg_.wallHalfWidth, up{0, 0, cfg_.wallHalfHeight};
		quad(h.pos - w - up, h.pos + w - up, h.pos + w + up, h.pos - w + up, n);
	}
}

std::vector<Surface> toSurfaces(const std::vector<Tri> &tris, const Frame &frame, uint16_t terrain)
{
	std::vector<Surface> out;
	out.reserve(tris.size());
	for (const Tri &t : tris)
	{
		V3 v[3] = {frame.toSm(t.a), frame.toSm(t.b), frame.toSm(t.c)};
		Surface s{0, 0, terrain, {}};
		for (int k = 0; k < 3; ++k)
		{
			s.v[k][0] = static_cast<int32_t>(std::lround(v[k].x));
			s.v[k][1] = static_cast<int32_t>(std::lround(v[k].y));
			s.v[k][2] = static_cast<int32_t>(std::lround(v[k].z));
		}
		const V3 p1(float(s.v[0][0]), float(s.v[0][1]), float(s.v[0][2]));
		const V3 p2(float(s.v[1][0]), float(s.v[1][1]), float(s.v[1][2]));
		const V3 p3(float(s.v[2][0]), float(s.v[2][1]), float(s.v[2][2]));
		const V3 n = cross(p2 - p1, p3 - p2); // SM64's own normal
		if (length(n) < 1e-3f)
			continue;
		if (dot(n, Frame::dirToSm(t.n)) < 0)
			for (int k = 0; k < 3; ++k)
				std::swap(s.v[1][k], s.v[2][k]);
		out.push_back(s);
	}
	return out;
}

std::vector<Surface> vehicleBox(const V3 &mn, const V3 &mx, float scale)
{
	std::vector<Tri> t;
	auto quad = [&](const V3 &a, const V3 &b, const V3 &c, const V3 &d, const V3 &n) {
		t.push_back({a, b, c, n});
		t.push_back({a, c, d, n});
	};
	const float x0 = mn.x, x1 = mx.x, y0 = mn.y, y1 = mx.y, z0 = mn.z, z1 = mx.z;
	quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});   // roof
	quad({x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0}, {0, 0, -1});  // underside
	quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1}, {1, 0, 0});   // right
	quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0});  // left
	quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {0, 1, 0});   // front
	quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});  // back
	Frame local;
	local.scale = scale;
	return toSurfaces(t, local);
}

float smObjectYawFromGtaHeading(float headingDeg)
{
	// libsm64 negates the angle it's given (CONVERT_ANGLE) and SM64 rotates about +Y the right-handed way,
	// which is GTA's rotation about +Z after the axis swap, so the two cancel out.
	return headingDeg;
}
} // namespace m64
