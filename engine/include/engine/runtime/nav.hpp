#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Triangle navmesh. Pathfinding is A* on triangle centroids + portal straightening.
 */
class NavMesh {
public:
	void Build(const Vec3* vertices, const std::size_t vertex_count, const std::uint32_t* indices, const std::size_t index_count);

	/**
	 * Find a path. Writes up to cap waypoints into out_points. Returns count (0 = none).
	 */
	int FindPath(const Vec3 start, const Vec3 goal, Vec3* out_points, const int cap) const;

	int TriangleCount() const { return static_cast<int>(tris_.size()); }
	bool Empty() const { return tris_.empty(); }

private:
	struct Tri {
		Vec3 v0{};
		Vec3 v1{};
		Vec3 v2{};
		Vec3 center{};
		int n[3]{-1, -1, -1};
	};

	int Locate(const Vec3 p) const;
	float Heuristic(const int a, const int b) const;

	std::vector<Tri> tris_{};
};

} // namespace hyperlite
