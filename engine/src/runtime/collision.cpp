#include "engine/runtime/collision.hpp"

#include <algorithm>
#include <cmath>

namespace hyperlite {

int CollisionWorld::Add(const Collider collider) {
	colliders_.push_back(collider);
	return static_cast<int>(colliders_.size()) - 1;
}

void CollisionWorld::Remove(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= colliders_.size()) {
		return;
	}
	colliders_[static_cast<std::size_t>(id)].aabb = {};
	colliders_[static_cast<std::size_t>(id)].sphere.radius = 0.0f;
}

Collider* CollisionWorld::Get(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= colliders_.size()) {
		return nullptr;
	}
	return &colliders_[static_cast<std::size_t>(id)];
}

const Collider* CollisionWorld::Get(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= colliders_.size()) {
		return nullptr;
	}
	return &colliders_[static_cast<std::size_t>(id)];
}

bool CollisionWorld::IntersectRay(const Ray ray, const Collider& c, float& t, Vec3& n) const {
	switch (c.kind) {
	case ColliderKind::Aabb: {
		if (!RayAabb(ray, c.aabb, t)) {
			return false;
		}
		const Vec3 p = ray.origin + ray.direction * t;
		const Vec3 cp = ClosestPointAabb(c.aabb, p);
		n = Normalize(p - AabbCenter(c.aabb));
		(void)cp;
		return true;
	}
	case ColliderKind::Sphere:
		if (!RaySphere(ray, c.sphere, t)) {
			return false;
		}
		n = Normalize((ray.origin + ray.direction * t) - c.sphere.center);
		return true;
	case ColliderKind::Plane:
		if (!RayPlane(ray, c.plane, t)) {
			return false;
		}
		n = c.plane.normal;
		return true;
	case ColliderKind::Triangle:
		if (!RayTriangle(ray, c.tri[0], c.tri[1], c.tri[2], t)) {
			return false;
		}
		n = Normalize(Cross(c.tri[1] - c.tri[0], c.tri[2] - c.tri[0]));
		return true;
	case ColliderKind::Capsule: {
		const Vec3 closest = ClosestPointSegment(c.capsule.a, c.capsule.b, ray.origin);
		Sphere s{closest, c.capsule.radius};
		if (!RaySphere(ray, s, t)) {
			return false;
		}
		n = Normalize((ray.origin + ray.direction * t) - closest);
		return true;
	}
	default:
		return false;
	}
}

Hit CollisionWorld::Raycast(const Ray ray, const float max_t) const {
	Hit best{};
	best.t = max_t;
	for (std::size_t i = 0; i < colliders_.size(); ++i) {
		float t = 0.0f;
		Vec3 n{};
		if (!IntersectRay(ray, colliders_[i], t, n) || t > max_t || t < 0.0f) {
			continue;
		}
		if (!best.hit || t < best.t) {
			best.hit = true;
			best.t = t;
			best.point = ray.origin + ray.direction * t;
			best.normal = n;
			best.collider = static_cast<int>(i);
		}
	}
	return best;
}

Hit CollisionWorld::SphereCast(const Sphere sphere, const Vec3 direction, const float max_t) const {
	Ray ray{sphere.center, direction};
	Hit hit = Raycast(ray, max_t);
	if (hit.hit) {
		hit.t = std::max(0.0f, hit.t - sphere.radius / std::max(Length(direction), 1.0e-6f));
		hit.point = sphere.center + direction * hit.t;
	}
	return hit;
}

int CollisionWorld::Overlap(const Collider& query, int* out_ids, const int cap) const {
	int written = 0;
	for (std::size_t i = 0; i < colliders_.size(); ++i) {
		const Collider& c = colliders_[i];
		bool hit = false;
		if (query.kind == ColliderKind::Aabb && c.kind == ColliderKind::Aabb) {
			hit = AabbOverlap(query.aabb, c.aabb);
		} else if (query.kind == ColliderKind::Sphere && c.kind == ColliderKind::Sphere) {
			hit = SphereOverlap(query.sphere, c.sphere);
		} else if (query.kind == ColliderKind::Sphere && c.kind == ColliderKind::Aabb) {
			const Vec3 p = ClosestPointAabb(c.aabb, query.sphere.center);
			hit = LengthSq(p - query.sphere.center) <= query.sphere.radius * query.sphere.radius;
		} else if (query.kind == ColliderKind::Aabb && c.kind == ColliderKind::Sphere) {
			const Vec3 p = ClosestPointAabb(query.aabb, c.sphere.center);
			hit = LengthSq(p - c.sphere.center) <= c.sphere.radius * c.sphere.radius;
		} else {
			hit = AabbOverlap(query.aabb, c.aabb);
		}
		if (hit && written < cap) {
			out_ids[written++] = static_cast<int>(i);
		}
	}
	return written;
}

Hit CollisionWorld::Sweep(const Collider& query, const Vec3 motion) const {
	const float len = Length(motion);
	if (len < 1.0e-8f) {
		return {};
	}
	Sphere s = query.sphere;
	if (query.kind == ColliderKind::Aabb) {
		s.center = AabbCenter(query.aabb);
		s.radius = Length(AabbExtents(query.aabb));
	}
	return SphereCast(s, motion * (1.0f / len), len);
}

Vec3 CollisionWorld::ClosestPoint(const int collider_id, const Vec3 point) const {
	const Collider* c = Get(collider_id);
	if (c == nullptr) {
		return point;
	}
	switch (c->kind) {
	case ColliderKind::Aabb:
		return ClosestPointAabb(c->aabb, point);
	case ColliderKind::Sphere: {
		const Vec3 d = point - c->sphere.center;
		const float len = Length(d);
		if (len < 1.0e-8f) {
			return c->sphere.center;
		}
		return c->sphere.center + d * (c->sphere.radius / len);
	}
	case ColliderKind::Capsule:
		return ClosestPointSegment(c->capsule.a, c->capsule.b, point);
	case ColliderKind::Plane: {
		const float dist = PlaneDistance(c->plane, point);
		return point - c->plane.normal * dist;
	}
	case ColliderKind::Triangle:
		return ClosestPointSegment(c->tri[0], c->tri[1], point);
	default:
		return point;
	}
}

void CollisionWorld::RaycastBatch(const Ray* rays, const std::size_t count, const float max_t, Hit* hits) const {
	if (rays == nullptr || hits == nullptr) {
		return;
	}
	for (std::size_t i = 0; i < count; ++i) {
		hits[i] = Raycast(rays[i], max_t);
	}
}

} // namespace hyperlite
