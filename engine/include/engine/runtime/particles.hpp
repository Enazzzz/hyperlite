#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

class Engine;

/**
 * SoA particle. Native storage; Python never holds a particle object.
 */
struct ParticleEmitterDesc {
	Vec3 origin{};
	Vec3 velocity{0.0f, 1.0f, 0.0f};
	Vec3 velocity_jitter{0.2f, 0.2f, 0.2f};
	Vec3 acceleration{0.0f, -2.0f, 0.0f};
	float life = 1.0f;
	float life_jitter = 0.25f;
	float size = 4.0f;
	std::uint32_t color = 0xFFFFFFFFu;
	int burst = 0;
	float spawn_rate = 0.0f;
	int capacity = 256;
};

/**
 * Native particle pool with batch update and 2D/point render helpers.
 */
class ParticleSystem {
public:
	int CreateEmitter(const ParticleEmitterDesc& desc);
	void DestroyEmitter(const int id);
	ParticleEmitterDesc* Emitter(const int id);

	/** Spawn `count` particles from emitter (pooling). */
	void Emit(const int emitter_id, const int count);

	/** Integrate all living particles. */
	void Update(const float dt);

	/**
	 * Draw living particles as screen-space rects (requires camera + viewport).
	 */
	void RenderPoints(Engine& engine, const Mat4& view_proj, const int width, const int height) const;

	int AliveCount() const { return alive_count_; }

private:
	struct EmitterSlot {
		ParticleEmitterDesc desc{};
		bool alive = false;
		float spawn_accum = 0.0f;
	};

	std::vector<EmitterSlot> emitters_{};
	std::vector<Vec3> pos_{};
	std::vector<Vec3> vel_{};
	std::vector<float> life_{};
	std::vector<float> max_life_{};
	std::vector<float> size_{};
	std::vector<std::uint32_t> color_{};
	std::vector<int> emitter_of_{};
	int alive_count_ = 0;

	void AddParticle(const int emitter_id, const Vec3 p, const Vec3 v, const float life, const float size, const std::uint32_t color);
};

} // namespace hyperlite
