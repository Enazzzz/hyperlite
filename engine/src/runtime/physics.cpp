#include "engine/runtime/physics.hpp"

namespace hyperlite {

int PhysicsWorld::AddBody(const RigidBody body) {
	bodies_.push_back(body);
	return static_cast<int>(bodies_.size()) - 1;
}

void PhysicsWorld::RemoveBody(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= bodies_.size()) {
		return;
	}
	bodies_[static_cast<std::size_t>(id)].awake = false;
	bodies_[static_cast<std::size_t>(id)].mass = 0.0f;
}

RigidBody* PhysicsWorld::GetBody(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= bodies_.size()) {
		return nullptr;
	}
	return &bodies_[static_cast<std::size_t>(id)];
}

const RigidBody* PhysicsWorld::GetBody(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= bodies_.size()) {
		return nullptr;
	}
	return &bodies_[static_cast<std::size_t>(id)];
}

void PhysicsWorld::ApplyForce(const int id, const Vec3 force) {
	RigidBody* b = GetBody(id);
	if (b != nullptr) {
		b->force = b->force + force;
	}
}

void PhysicsWorld::ApplyImpulse(const int id, const Vec3 impulse) {
	RigidBody* b = GetBody(id);
	if (b != nullptr && b->mass > 1.0e-8f) {
		b->velocity = b->velocity + impulse * (1.0f / b->mass);
	}
}

void PhysicsWorld::Substep(const float dt) {
	last_triggers_ = 0;
	for (RigidBody& b : bodies_) {
		if (!b.awake || b.kind != BodyKind::Dynamic || b.mass <= 1.0e-8f) {
			continue;
		}
		b.force = b.force + gravity_ * b.mass;
		b.velocity = b.velocity + b.force * (dt / b.mass);
		const Vec3 motion = b.velocity * dt;
		if (b.collider >= 0) {
			Collider* col = colliders_.Get(b.collider);
			if (col != nullptr) {
				const Hit hit = colliders_.Sweep(*col, motion);
				if (hit.hit && hit.t < Length(motion)) {
					if (col->trigger || (hit.collider >= 0 && colliders_.Get(hit.collider) &&
						colliders_.Get(hit.collider)->trigger)) {
						++last_triggers_;
					} else {
						b.position = b.position + Normalize(motion) * (hit.t * 0.95f);
						const float vn = Dot(b.velocity, hit.normal);
						if (vn < 0.0f) {
							b.velocity = b.velocity - hit.normal * ((1.0f + b.restitution) * vn);
						}
						b.force = {};
						continue;
					}
				}
			}
		}
		b.position = b.position + motion;
		b.force = {};
		if (b.collider >= 0) {
			Collider* col = colliders_.Get(b.collider);
			if (col != nullptr) {
				if (col->kind == ColliderKind::Sphere) {
					col->sphere.center = b.position;
				} else if (col->kind == ColliderKind::Aabb) {
					const Vec3 e = AabbExtents(col->aabb);
					col->aabb.min = b.position - e;
					col->aabb.max = b.position + e;
				}
			}
		}
	}
}

void PhysicsWorld::Step(const float real_dt, const int max_substeps) {
	accum_ += real_dt;
	int steps = 0;
	while (accum_ >= fixed_dt_ && steps < max_substeps) {
		Substep(fixed_dt_);
		accum_ -= fixed_dt_;
		++steps;
	}
	if (steps == max_substeps) {
		accum_ = 0.0f;
	}
}

Vec3 PhysicsWorld::MoveCharacter(const Capsule capsule, const Vec3 motion, const float skin) {
	const float len = Length(motion);
	if (len < 1.0e-8f) {
		return capsule.a;
	}
	Sphere s{(capsule.a + capsule.b) * 0.5f, capsule.radius + skin};
	const Hit hit = colliders_.SphereCast(s, motion * (1.0f / len), len);
	if (!hit.hit) {
		return capsule.a + motion;
	}
	Vec3 slide = motion - hit.normal * Dot(motion, hit.normal);
	return capsule.a + Normalize(motion) * (hit.t * 0.95f) + slide * 0.25f;
}

} // namespace hyperlite
