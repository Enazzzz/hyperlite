#include "engine/runtime/lighting.hpp"

#include <algorithm>
#include <cmath>

namespace hyperlite {

int LightSet::Add(const Light light) {
	lights_.push_back(light);
	return static_cast<int>(lights_.size()) - 1;
}

void LightSet::Remove(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= lights_.size()) {
		return;
	}
	lights_[static_cast<std::size_t>(id)].intensity = 0.0f;
}

Light* LightSet::Get(const int id) {
	if (id < 0 || static_cast<std::size_t>(id) >= lights_.size()) {
		return nullptr;
	}
	return &lights_[static_cast<std::size_t>(id)];
}

const Light* LightSet::Get(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= lights_.size()) {
		return nullptr;
	}
	return &lights_[static_cast<std::size_t>(id)];
}

Vec3 LightSet::Evaluate(const Vec3 position, const Vec3 normal, const float shadow) const {
	Vec3 rgb{0.0f, 0.0f, 0.0f};
	const Vec3 n = Normalize(normal);
	for (const Light& L : lights_) {
		if (L.intensity <= 0.0f) {
			continue;
		}
		Vec3 contrib = L.color * L.intensity;
		if (L.kind == LightKind::Ambient) {
			rgb = rgb + contrib;
			continue;
		}
		Vec3 ldir{};
		float atten = 1.0f;
		if (L.kind == LightKind::Directional) {
			ldir = Normalize(L.direction * -1.0f);
		} else {
			const Vec3 to_l = L.position - position;
			const float dist = Length(to_l);
			ldir = dist > 1.0e-12f ? to_l * (1.0f / dist) : Vec3{};
			atten = Clamp(1.0f - dist / std::max(L.range, 1.0e-3f), 0.0f, 1.0f);
			if (L.kind == LightKind::Spot) {
				const float c = Dot(Normalize(L.direction * -1.0f), ldir);
				const float inner = std::cos(L.inner_cone);
				const float outer = std::cos(L.outer_cone);
				atten *= Clamp((c - outer) / std::max(inner - outer, 1.0e-4f), 0.0f, 1.0f);
			}
		}
		const float ndotl = std::max(0.0f, Dot(n, ldir));
		const float sh = L.casts_shadow ? shadow : 1.0f;
		rgb = rgb + contrib * (ndotl * atten * sh);
	}
	return rgb;
}

void LightSet::EvaluateBatch(
	const Vec3* positions,
	const Vec3* normals,
	const std::size_t count,
	const float* shadows,
	float* out_rgb) const {
	if (positions == nullptr || normals == nullptr || out_rgb == nullptr) {
		return;
	}
	for (std::size_t i = 0; i < count; ++i) {
		const float sh = shadows != nullptr ? shadows[i] : 1.0f;
		const Vec3 c = Evaluate(positions[i], normals[i], sh);
		out_rgb[i * 3U] = c.x;
		out_rgb[i * 3U + 1U] = c.y;
		out_rgb[i * 3U + 2U] = c.z;
	}
}

void ShadowMap::Resize(const int width, const int height) {
	width_ = std::max(1, width);
	height_ = std::max(1, height);
	color_.Resize(width_, height_);
	depth_.Resize(width_, height_);
}

void ShadowMap::SetupDirectional(const Vec3 direction, const Aabb world_bounds, const float padding) {
	const Vec3 center = AabbCenter(world_bounds);
	const Vec3 ext = AabbExtents(world_bounds);
	const float radius = Length(ext) + padding;
	const Vec3 dir = Normalize(direction);
	const Vec3 eye = center - dir * (radius + 1.0f);
	camera_.LookAt(eye, center, {0.0f, 1.0f, 0.0f});
	camera_.SetOrthographic(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
}

float ShadowMap::Sample(const Vec3 world, const float bias) const {
	if (width_ <= 0 || height_ <= 0 || !depth_.Allocated()) {
		return 1.0f;
	}
	const Mat4 vp = camera_.ViewProj();
	const float x = vp.m[0] * world.x + vp.m[4] * world.y + vp.m[8] * world.z + vp.m[12];
	const float y = vp.m[1] * world.x + vp.m[5] * world.y + vp.m[9] * world.z + vp.m[13];
	const float z = vp.m[2] * world.x + vp.m[6] * world.y + vp.m[10] * world.z + vp.m[14];
	const float w = vp.m[3] * world.x + vp.m[7] * world.y + vp.m[11] * world.z + vp.m[15];
	if (std::fabs(w) < 1.0e-12f) {
		return 1.0f;
	}
	const float ndc_x = x / w;
	const float ndc_y = y / w;
	const float ndc_z = z / w;
	const int px = static_cast<int>((ndc_x * 0.5f + 0.5f) * static_cast<float>(width_));
	const int py = static_cast<int>((1.0f - (ndc_y * 0.5f + 0.5f)) * static_cast<float>(height_));
	if (px < 0 || py < 0 || px >= width_ || py >= height_) {
		return 1.0f;
	}
	const float d = depth_.At(px, py);
	const float z01 = ndc_z * 0.5f + 0.5f;
	return z01 <= d + bias ? 1.0f : 0.0f;
}

} // namespace hyperlite
