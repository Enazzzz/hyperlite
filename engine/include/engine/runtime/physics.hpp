#pragma once

#include "engine/runtime/collision.hpp"
#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

enum class BodyKind : int {
	Dynamic = 0,
	Kinematic = 1,
	Static = 2,
};

struct RigidBody {
	BodyKind kind = BodyKind::Dynamic;
	Vec3 position{};
	Vec3 velocity{};
	Vec3 force{};
	float mass = 1.0f;
	float restitution = 0.0f;
	int collider = -1;
	bool awake = true;
};

/**
 * Optional fixed-timestep physics world. Never steps unless the programmer calls Step.
 */
class PhysicsWorld {
public:
	void SetGravity(const Vec3 g) { gravity_ = g; }
	Vec3 Gravity() const { return gravity_; }
	void SetFixedDt(const float dt) { fixed_dt_ = dt; }
	float FixedDt() const { return fixed_dt_; }

	CollisionWorld& Colliders() { return colliders_; }
	const CollisionWorld& Colliders() const { return colliders_; }

	int AddBody(const RigidBody body);
	void RemoveBody(const int id);
	RigidBody* GetBody(const int id);
	const RigidBody* GetBody(const int id) const;

	void ApplyForce(const int id, const Vec3 force);
	void ApplyImpulse(const int id, const Vec3 impulse);

	/**
	 * Accumulate real time and step a fixed number of substeps (max_substeps cap).
	 */
	void Step(const float real_dt, const int max_substeps = 8);

	/**
	 * Character controller: move a capsule with slide against colliders.
	 */
	Vec3 MoveCharacter(const Capsule capsule, const Vec3 motion, const float skin = 0.02f);

	int TriggerCount() const { return last_triggers_; }

private:
	void Substep(const float dt);

	CollisionWorld colliders_{};
	std::vector<RigidBody> bodies_{};
	Vec3 gravity_{0.0f, -9.81f, 0.0f};
	float fixed_dt_ = 1.0f / 60.0f;
	float accum_ = 0.0f;
	int last_triggers_ = 0;
};

} // namespace hyperlite
