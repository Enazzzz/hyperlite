#pragma once

#include "engine/runtime/math.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

enum class ColliderKind : int {
	Aabb = 0,
	Sphere = 1,
	Capsule = 2,
	Plane = 3,
	Triangle = 4,
};

struct Collider {
	ColliderKind kind = ColliderKind::Aabb;
	Aabb aabb{};
	Sphere sphere{};
	Capsule capsule{};
	Plane plane{};
	Vec3 tri[3]{};
	bool trigger = false;
	std::uint32_t layer = 1;
};

struct Hit {
	bool hit = false;
	float t = 0.0f;
	Vec3 point{};
	Vec3 normal{};
	int collider = -1;
};

/**
 * Collision primitives and queries (ray, sphere-cast, overlap, sweep, closest point).
 */
class CollisionWorld {
public:
	int Add(const Collider collider);
	void Remove(const int id);
	Collider* Get(const int id);
	const Collider* Get(const int id) const;
	const std::vector<Collider>& All() const { return colliders_; }

	Hit Raycast(const Ray ray, const float max_t = 1.0e9f) const;
	Hit SphereCast(const Sphere sphere, const Vec3 direction, const float max_t) const;
	int Overlap(const Collider& query, int* out_ids, const int cap) const;
	Hit Sweep(const Collider& query, const Vec3 motion) const;
	Vec3 ClosestPoint(const int collider_id, const Vec3 point) const;

	/** Batch raycasts: rays[i] → hits[i]. */
	void RaycastBatch(const Ray* rays, const std::size_t count, const float max_t, Hit* hits) const;

private:
	std::vector<Collider> colliders_{};
	bool IntersectRay(const Ray ray, const Collider& c, float& t, Vec3& n) const;
};

} // namespace hyperlite
