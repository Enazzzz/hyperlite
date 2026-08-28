#include "engine/runtime/spatial.hpp"

#include <algorithm>
#include <cmath>

namespace hyperlite {

std::uint64_t SpatialHash::Key(const std::int32_t x, const std::int32_t y, const std::int32_t z) const {
	return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 73856093ull) ^
		(static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 19349663ull) ^
		(static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 83492791ull);
}

void SpatialHash::CellRange(
	const Aabb b,
	std::int32_t& x0,
	std::int32_t& y0,
	std::int32_t& z0,
	std::int32_t& x1,
	std::int32_t& y1,
	std::int32_t& z1) const {
	const float inv = 1.0f / cell_;
	x0 = static_cast<std::int32_t>(std::floor(b.min.x * inv));
	y0 = static_cast<std::int32_t>(std::floor(b.min.y * inv));
	z0 = static_cast<std::int32_t>(std::floor(b.min.z * inv));
	x1 = static_cast<std::int32_t>(std::floor(b.max.x * inv));
	y1 = static_cast<std::int32_t>(std::floor(b.max.y * inv));
	z1 = static_cast<std::int32_t>(std::floor(b.max.z * inv));
}

SpatialHash::Cell* SpatialHash::Find(const std::int32_t x, const std::int32_t y, const std::int32_t z) {
	for (auto& c : cells_) {
		if (c.cx == x && c.cy == y && c.cz == z) {
			return &c;
		}
	}
	return nullptr;
}

const SpatialHash::Cell* SpatialHash::Find(const std::int32_t x, const std::int32_t y, const std::int32_t z) const {
	for (const auto& c : cells_) {
		if (c.cx == x && c.cy == y && c.cz == z) {
			return &c;
		}
	}
	return nullptr;
}

void SpatialHash::Clear() {
	cells_.clear();
	bounds_.clear();
}

void SpatialHash::Insert(const int id, const Aabb bounds) {
	if (id >= static_cast<int>(bounds_.size())) {
		bounds_.resize(static_cast<std::size_t>(id) + 1U);
	}
	bounds_[static_cast<std::size_t>(id)] = bounds;
	std::int32_t x0, y0, z0, x1, y1, z1;
	CellRange(bounds, x0, y0, z0, x1, y1, z1);
	for (std::int32_t z = z0; z <= z1; ++z) {
		for (std::int32_t y = y0; y <= y1; ++y) {
			for (std::int32_t x = x0; x <= x1; ++x) {
				Cell* cell = Find(x, y, z);
				if (cell == nullptr) {
					Cell created{};
					created.cx = x;
					created.cy = y;
					created.cz = z;
					created.ids.push_back(id);
					cells_.push_back(created);
				} else {
					cell->ids.push_back(id);
				}
			}
		}
	}
}

void SpatialHash::InsertSphere(const int id, const Sphere s) {
	Aabb b{};
	b.min = s.center - Vec3{s.radius, s.radius, s.radius};
	b.max = s.center + Vec3{s.radius, s.radius, s.radius};
	Insert(id, b);
}

int SpatialHash::QueryAabb(const Aabb bounds, int* out_ids, const int cap) const {
	if (out_ids == nullptr || cap <= 0) {
		return 0;
	}
	std::int32_t x0, y0, z0, x1, y1, z1;
	CellRange(bounds, x0, y0, z0, x1, y1, z1);
	int written = 0;
	for (std::int32_t z = z0; z <= z1; ++z) {
		for (std::int32_t y = y0; y <= y1; ++y) {
			for (std::int32_t x = x0; x <= x1; ++x) {
				const Cell* cell = Find(x, y, z);
				if (cell == nullptr) {
					continue;
				}
				for (const int id : cell->ids) {
					bool dup = false;
					for (int k = 0; k < written; ++k) {
						if (out_ids[k] == id) {
							dup = true;
							break;
						}
					}
					if (!dup && written < cap) {
						out_ids[written++] = id;
					}
				}
			}
		}
	}
	return written;
}

int SpatialHash::QuerySphere(const Sphere s, int* out_ids, const int cap) const {
	Aabb b{};
	b.min = s.center - Vec3{s.radius, s.radius, s.radius};
	b.max = s.center + Vec3{s.radius, s.radius, s.radius};
	return QueryAabb(b, out_ids, cap);
}

int SpatialHash::QueryRay(const Ray ray, const float max_t, int* out_ids, const int cap) const {
	const Vec3 end = ray.origin + ray.direction * max_t;
	Aabb b{};
	b.min = {std::min(ray.origin.x, end.x), std::min(ray.origin.y, end.y), std::min(ray.origin.z, end.z)};
	b.max = {std::max(ray.origin.x, end.x), std::max(ray.origin.y, end.y), std::max(ray.origin.z, end.z)};
	return QueryAabb(b, out_ids, cap);
}

int SpatialHash::QueryFrustum(
	const Frustum& frustum,
	const Aabb* bounds_by_id,
	const int id_count,
	int* out_ids,
	const int cap) const {
	if (bounds_by_id == nullptr || out_ids == nullptr) {
		return 0;
	}
	int written = 0;
	for (int i = 0; i < id_count && written < cap; ++i) {
		if (FrustumAabb(frustum, bounds_by_id[i])) {
			out_ids[written++] = i;
		}
	}
	return written;
}

int Bvh::BuildRange(int start, int count) {
	Node node{};
	node.start = start;
	node.count = count;
	node.bounds = bounds_[static_cast<std::size_t>(indices_[static_cast<std::size_t>(start)])];
	for (int i = 1; i < count; ++i) {
		AabbEncapsulate(node.bounds, bounds_[static_cast<std::size_t>(indices_[static_cast<std::size_t>(start + i)])].min);
		AabbEncapsulate(node.bounds, bounds_[static_cast<std::size_t>(indices_[static_cast<std::size_t>(start + i)])].max);
	}
	if (count <= 4) {
		node.leaf = true;
		const int idx = static_cast<int>(nodes_.size());
		nodes_.push_back(node);
		return idx;
	}
	const Vec3 e = AabbExtents(node.bounds);
	int axis = 0;
	if (e.y > e.x) {
		axis = 1;
	}
	if (e.z > (axis == 0 ? e.x : e.y)) {
		axis = 2;
	}
	const int mid = start + count / 2;
	std::nth_element(
		indices_.begin() + start,
		indices_.begin() + mid,
		indices_.begin() + start + count,
		[&](const int a, const int b) {
			const Vec3 ca = AabbCenter(bounds_[static_cast<std::size_t>(a)]);
			const Vec3 cb = AabbCenter(bounds_[static_cast<std::size_t>(b)]);
			const float* pa = &ca.x;
			const float* pb = &cb.x;
			return pa[axis] < pb[axis];
		});
	const int self = static_cast<int>(nodes_.size());
	nodes_.push_back(node);
	nodes_[static_cast<std::size_t>(self)].left = BuildRange(start, mid - start);
	nodes_[static_cast<std::size_t>(self)].right = BuildRange(mid, start + count - mid);
	return self;
}

void Bvh::Build(const Aabb* bounds, const int count) {
	nodes_.clear();
	indices_.clear();
	bounds_.clear();
	if (bounds == nullptr || count <= 0) {
		return;
	}
	bounds_.assign(bounds, bounds + count);
	indices_.resize(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i) {
		indices_[static_cast<std::size_t>(i)] = i;
	}
	BuildRange(0, count);
}

void Bvh::RayRecurse(const int node, const Ray ray, const float max_t, int* out_ids, int& written, const int cap) const {
	if (node < 0 || written >= cap) {
		return;
	}
	const Node& n = nodes_[static_cast<std::size_t>(node)];
	float t = 0.0f;
	if (!RayAabb(ray, n.bounds, t) || t > max_t) {
		return;
	}
	if (n.leaf) {
		for (int i = 0; i < n.count && written < cap; ++i) {
			out_ids[written++] = indices_[static_cast<std::size_t>(n.start + i)];
		}
		return;
	}
	RayRecurse(n.left, ray, max_t, out_ids, written, cap);
	RayRecurse(n.right, ray, max_t, out_ids, written, cap);
}

void Bvh::FrustumRecurse(const int node, const Frustum& frustum, int* out_ids, int& written, const int cap) const {
	if (node < 0 || written >= cap) {
		return;
	}
	const Node& n = nodes_[static_cast<std::size_t>(node)];
	if (!FrustumAabb(frustum, n.bounds)) {
		return;
	}
	if (n.leaf) {
		for (int i = 0; i < n.count && written < cap; ++i) {
			out_ids[written++] = indices_[static_cast<std::size_t>(n.start + i)];
		}
		return;
	}
	FrustumRecurse(n.left, frustum, out_ids, written, cap);
	FrustumRecurse(n.right, frustum, out_ids, written, cap);
}

int Bvh::RayQuery(const Ray ray, const float max_t, int* out_ids, const int cap) const {
	if (nodes_.empty() || out_ids == nullptr) {
		return 0;
	}
	int written = 0;
	RayRecurse(0, ray, max_t, out_ids, written, cap);
	return written;
}

int Bvh::FrustumQuery(const Frustum& frustum, int* out_ids, const int cap) const {
	if (nodes_.empty() || out_ids == nullptr) {
		return 0;
	}
	int written = 0;
	FrustumRecurse(0, frustum, out_ids, written, cap);
	return written;
}

} // namespace hyperlite
