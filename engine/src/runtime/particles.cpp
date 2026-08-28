#include "engine/runtime/particles.hpp"

#include "engine/command_buffer.hpp"
#include "engine/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace hyperlite {

int ParticleSystem::CreateEmitter(const ParticleEmitterDesc& desc) {
	for (std::size_t i = 0; i < emitters_.size(); ++i) {
		if (!emitters_[i].alive) {
			emitters_[i].desc = desc;
			emitters_[i].alive = true;
			emitters_[i].spawn_accum = 0.0f;
			return static_cast<int>(i);
		}
	}
	EmitterSlot s{};
	s.desc = desc;
	s.alive = true;
	emitters_.push_back(s);
	return static_cast<int>(emitters_.size()) - 1;
}

void ParticleSystem::DestroyEmitter(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= emitters_.size()) {
		return;
	}
	emitters_[static_cast<std::size_t>(id)].alive = false;
}

ParticleEmitterDesc* ParticleSystem::Emitter(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= emitters_.size() || !emitters_[static_cast<std::size_t>(id)].alive) {
		return nullptr;
	}
	return &emitters_[static_cast<std::size_t>(id)].desc;
}

void ParticleSystem::AddParticle(
	const int emitter_id,
	const Vec3 p,
	const Vec3 v,
	const float life,
	const float size,
	const std::uint32_t color) {
	pos_.push_back(p);
	vel_.push_back(v);
	life_.push_back(life);
	max_life_.push_back(life);
	size_.push_back(size);
	color_.push_back(color);
	emitter_of_.push_back(emitter_id);
	++alive_count_;
}

void ParticleSystem::Emit(const int emitter_id, const int count) {
	if (emitter_id < 0 || static_cast<std::size_t>(emitter_id) >= emitters_.size()) {
		return;
	}
	const ParticleEmitterDesc& d = emitters_[static_cast<std::size_t>(emitter_id)].desc;
	for (int i = 0; i < count; ++i) {
		const float jx = (static_cast<float>(std::rand() % 2000) / 1000.0f - 1.0f);
		const float jy = (static_cast<float>(std::rand() % 2000) / 1000.0f - 1.0f);
		const float jz = (static_cast<float>(std::rand() % 2000) / 1000.0f - 1.0f);
		const Vec3 v{
			d.velocity.x + d.velocity_jitter.x * jx,
			d.velocity.y + d.velocity_jitter.y * jy,
			d.velocity.z + d.velocity_jitter.z * jz};
		const float life = std::max(0.05f, d.life + d.life_jitter * jx);
		AddParticle(emitter_id, d.origin, v, life, d.size, d.color);
	}
}

void ParticleSystem::Update(const float dt) {
	for (std::size_t e = 0; e < emitters_.size(); ++e) {
		if (!emitters_[e].alive) {
			continue;
		}
		ParticleEmitterDesc& d = emitters_[e].desc;
		if (d.burst > 0) {
			Emit(static_cast<int>(e), d.burst);
			d.burst = 0;
		}
		if (d.spawn_rate > 0.0f) {
			emitters_[e].spawn_accum += dt * d.spawn_rate;
			const int n = static_cast<int>(emitters_[e].spawn_accum);
			if (n > 0) {
				emitters_[e].spawn_accum -= static_cast<float>(n);
				Emit(static_cast<int>(e), n);
			}
		}
	}
	alive_count_ = 0;
	std::size_t w = 0;
	for (std::size_t i = 0; i < pos_.size(); ++i) {
		life_[i] -= dt;
		if (life_[i] <= 0.0f) {
			continue;
		}
		const int eid = emitter_of_[i];
		Vec3 acc{0.0f, -2.0f, 0.0f};
		if (eid >= 0 && static_cast<std::size_t>(eid) < emitters_.size()) {
			acc = emitters_[static_cast<std::size_t>(eid)].desc.acceleration;
		}
		vel_[i] = vel_[i] + acc * dt;
		pos_[i] = pos_[i] + vel_[i] * dt;
		if (w != i) {
			pos_[w] = pos_[i];
			vel_[w] = vel_[i];
			life_[w] = life_[i];
			max_life_[w] = max_life_[i];
			size_[w] = size_[i];
			color_[w] = color_[i];
			emitter_of_[w] = emitter_of_[i];
		}
		++w;
	}
	pos_.resize(w);
	vel_.resize(w);
	life_.resize(w);
	max_life_.resize(w);
	size_.resize(w);
	color_.resize(w);
	emitter_of_.resize(w);
	alive_count_ = static_cast<int>(w);
}

void ParticleSystem::RenderPoints(Engine& engine, const Mat4& view_proj, const int width, const int height) const {
	for (std::size_t i = 0; i < pos_.size(); ++i) {
		const Vec3 p = pos_[i];
		const float x = view_proj.m[0] * p.x + view_proj.m[4] * p.y + view_proj.m[8] * p.z + view_proj.m[12];
		const float y = view_proj.m[1] * p.x + view_proj.m[5] * p.y + view_proj.m[9] * p.z + view_proj.m[13];
		const float w = view_proj.m[3] * p.x + view_proj.m[7] * p.y + view_proj.m[11] * p.z + view_proj.m[15];
		if (w <= 1.0e-4f) {
			continue;
		}
		const int sx = static_cast<int>((x / w * 0.5f + 0.5f) * static_cast<float>(width));
		const int sy = static_cast<int>((1.0f - (y / w * 0.5f + 0.5f)) * static_cast<float>(height));
		const int s = std::max(1, static_cast<int>(size_[i]));
		engine.PushCommand(MakeDrawCommand(CommandType::kRectFill, sx - s / 2, sy - s / 2, s, s, color_[i]));
	}
}

} // namespace hyperlite
