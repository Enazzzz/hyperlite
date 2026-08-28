#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Uniform spatial hash / grid for broadphase queries.
 */
class SpatialHash {
public:
	explicit SpatialHash(const float cell = 4.0f) : cell_(cell) {}

	void Clear();
	void Insert(const int id, const Aabb bounds);
	void InsertSphere(const int id, const Sphere s);

	int QueryAabb(const Aabb bounds, int* out_ids, const int cap) const;
	int QuerySphere(const Sphere s, int* out_ids, const int cap) const;
	int QueryRay(const Ray ray, const float max_t, int* out_ids, const int cap) const;
	int QueryFrustum(const Frustum& frustum, const Aabb* bounds_by_id, const int id_count, int* out_ids, const int cap) const;

	float CellSize() const { return cell_; }

private:
	struct Cell {
		std::int32_t cx = 0;
		std::int32_t cy = 0;
		std::int32_t cz = 0;
		std::vector<int> ids{};
	};

	std::uint64_t Key(const std::int32_t x, const std::int32_t y, const std::int32_t z) const;
	Cell* Find(const std::int32_t x, const std::int32_t y, const std::int32_t z);
	const Cell* Find(const std::int32_t x, const std::int32_t y, const std::int32_t z) const;
	void CellRange(const Aabb b, std::int32_t& x0, std::int32_t& y0, std::int32_t& z0, std::int32_t& x1, std::int32_t& y1, std::int32_t& z1) const;

	float cell_ = 4.0f;
	std::vector<Cell> cells_{};
	std::vector<Aabb> bounds_{};
};

/**
 * Simple BVH (median split on longest axis) for ray / frustum queries.
 */
class Bvh {
public:
	void Build(const Aabb* bounds, const int count);
	int RayQuery(const Ray ray, const float max_t, int* out_ids, const int cap) const;
	int FrustumQuery(const Frustum& frustum, int* out_ids, const int cap) const;
	bool Empty() const { return nodes_.empty(); }

private:
	struct Node {
		Aabb bounds{};
		int left = -1;
		int right = -1;
		int start = 0;
		int count = 0;
		bool leaf = false;
	};

	int BuildRange(int start, int count);
	void RayRecurse(const int node, const Ray ray, const float max_t, int* out_ids, int& written, const int cap) const;
	void FrustumRecurse(const int node, const Frustum& frustum, int* out_ids, int& written, const int cap) const;

	std::vector<Node> nodes_{};
	std::vector<int> indices_{};
	std::vector<Aabb> bounds_{};
};

} // namespace hyperlite
