#pragma once

#include "engine/runtime/collision.hpp"
#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

class Engine;
class Camera;

/**
 * Debug overlays drawn through Engine line/tri APIs. Explicit calls only.
 */
class DebugDraw {
public:
	void WireAabb(Engine& engine, const Aabb b, const std::uint32_t color);
	void WireSphere(Engine& engine, const Sphere s, const std::uint32_t color, const int segments = 24);
	void WireFrustum(Engine& engine, const Camera& camera, const std::uint32_t color);
	void WireCapsule(Engine& engine, const Capsule c, const std::uint32_t color);
	void WireCollider(Engine& engine, const Collider& c, const std::uint32_t color);

	void DepthHeatmap(Engine& engine, const float* depth, const int width, const int height);
	void TileGrid(Engine& engine, const int width, const int height, const int tile, const std::uint32_t color);
	void Normals(Engine& engine, const Vec3* origins, const Vec3* normals, const int count, const float length, const std::uint32_t color);

	void HiZTiles(
		Engine& engine,
		const float* tile_max,
		const int tiles_x,
		const int tiles_y,
		const int tile_size,
		const int fb_w,
		const int fb_h);

private:
	void Line3(Engine& engine, const Vec3 a, const Vec3 b, const std::uint32_t color);
};

} // namespace hyperlite
