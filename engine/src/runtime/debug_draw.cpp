#include "engine/runtime/debug_draw.hpp"

#include "engine/command_buffer.hpp"
#include "engine/engine.hpp"
#include "engine/runtime/camera.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hyperlite {

void DebugDraw::Line3(Engine& engine, const Vec3 a, const Vec3 b, const std::uint32_t color) {
	float segs[6] = {a.x, a.y, a.z, b.x, b.y, b.z};
	engine.Lines3d(segs, 1, color, 1);
}

void DebugDraw::WireAabb(Engine& engine, const Aabb b, const std::uint32_t color) {
	const Vec3 p[8] = {
		{b.min.x, b.min.y, b.min.z}, {b.max.x, b.min.y, b.min.z},
		{b.max.x, b.max.y, b.min.z}, {b.min.x, b.max.y, b.min.z},
		{b.min.x, b.min.y, b.max.z}, {b.max.x, b.min.y, b.max.z},
		{b.max.x, b.max.y, b.max.z}, {b.min.x, b.max.y, b.max.z},
	};
	const int e[12][2] = {
		{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
	for (int i = 0; i < 12; ++i) {
		Line3(engine, p[e[i][0]], p[e[i][1]], color);
	}
}

void DebugDraw::WireSphere(Engine& engine, const Sphere s, const std::uint32_t color, const int segments) {
	const int n = std::max(8, segments);
	for (int axis = 0; axis < 3; ++axis) {
		Vec3 prev{};
		for (int i = 0; i <= n; ++i) {
			const float a = static_cast<float>(i) / static_cast<float>(n) * 6.2831853f;
			Vec3 p = s.center;
			if (axis == 0) {
				p.y += std::cos(a) * s.radius;
				p.z += std::sin(a) * s.radius;
			} else if (axis == 1) {
				p.x += std::cos(a) * s.radius;
				p.z += std::sin(a) * s.radius;
			} else {
				p.x += std::cos(a) * s.radius;
				p.y += std::sin(a) * s.radius;
			}
			if (i > 0) {
				Line3(engine, prev, p, color);
			}
			prev = p;
		}
	}
}

void DebugDraw::WireFrustum(Engine& engine, const Camera& camera, const std::uint32_t color) {
	const Mat4 inv = Mat4AffineInverse(camera.ViewProj());
	Vec3 c[8];
	int i = 0;
	for (int z = -1; z <= 1; z += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int x = -1; x <= 1; x += 2) {
				c[i++] = TransformPoint(inv, {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
			}
		}
	}
	const int e[][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
	for (const auto& edge : e) {
		Line3(engine, c[edge[0]], c[edge[1]], color);
	}
}

void DebugDraw::WireCapsule(Engine& engine, const Capsule c, const std::uint32_t color) {
	WireSphere(engine, {c.a, c.radius}, color, 16);
	WireSphere(engine, {c.b, c.radius}, color, 16);
	Line3(engine, c.a, c.b, color);
}

void DebugDraw::WireCollider(Engine& engine, const Collider& c, const std::uint32_t color) {
	switch (c.kind) {
	case ColliderKind::Aabb:
		WireAabb(engine, c.aabb, color);
		break;
	case ColliderKind::Sphere:
		WireSphere(engine, c.sphere, color);
		break;
	case ColliderKind::Capsule:
		WireCapsule(engine, c.capsule, color);
		break;
	case ColliderKind::Triangle:
		Line3(engine, c.tri[0], c.tri[1], color);
		Line3(engine, c.tri[1], c.tri[2], color);
		Line3(engine, c.tri[2], c.tri[0], color);
		break;
	case ColliderKind::Plane:
		break;
	}
}

void DebugDraw::DepthHeatmap(Engine& engine, const float* depth, const int width, const int height) {
	if (depth == nullptr) {
		return;
	}
	for (int y = 0; y < height; y += 4) {
		for (int x = 0; x < width; x += 4) {
			const float z = depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
			const std::uint8_t g = static_cast<std::uint8_t>(Clamp(z, 0.0f, 1.0f) * 255.0f);
			const std::uint32_t packed = PackColor({g, static_cast<std::uint8_t>(255 - g), 40, 255});
			engine.PushCommand(MakeDrawCommand(CommandType::kRectFill, x, y, 4, 4, packed));
		}
	}
}

void DebugDraw::TileGrid(Engine& engine, const int width, const int height, const int tile, const std::uint32_t color) {
	const int t = std::max(1, tile);
	for (int x = 0; x < width; x += t) {
		engine.PushCommand(MakeDrawCommand(CommandType::kLine, x, 0, x, height - 1, color));
	}
	for (int y = 0; y < height; y += t) {
		engine.PushCommand(MakeDrawCommand(CommandType::kLine, 0, y, width - 1, y, color));
	}
}

void DebugDraw::Normals(
	Engine& engine,
	const Vec3* origins,
	const Vec3* normals,
	const int count,
	const float length,
	const std::uint32_t color) {
	if (origins == nullptr || normals == nullptr) {
		return;
	}
	for (int i = 0; i < count; ++i) {
		Line3(engine, origins[i], origins[i] + normals[i] * length, color);
	}
}

void DebugDraw::HiZTiles(
	Engine& engine,
	const float* tile_max,
	const int tiles_x,
	const int tiles_y,
	const int tile_size,
	const int fb_w,
	const int fb_h) {
	if (tile_max == nullptr) {
		return;
	}
	for (int ty = 0; ty < tiles_y; ++ty) {
		for (int tx = 0; tx < tiles_x; ++tx) {
			const float z = tile_max[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tiles_x) + static_cast<std::size_t>(tx)];
			const std::uint8_t g = static_cast<std::uint8_t>(Clamp(z, 0.0f, 1.0f) * 255.0f);
			const int x0 = tx * tile_size;
			const int y0 = ty * tile_size;
			engine.PushCommand(MakeDrawCommand(
				CommandType::kRectOutline, x0, y0, std::min(tile_size, fb_w - x0), std::min(tile_size, fb_h - y0),
				PackColor({g, 40, static_cast<std::uint8_t>(255 - g), 255})));
		}
	}
}

} // namespace hyperlite
