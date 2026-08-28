#pragma once

#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"
#include "engine/runtime/camera.hpp"
#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

enum class LightKind : int {
	Directional = 0,
	Point = 1,
	Spot = 2,
	Ambient = 3,
};

/**
 * CPU light. Color is linear RGB in [0,1]; intensity scales it.
 */
struct Light {
	LightKind kind = LightKind::Directional;
	Vec3 position{};
	Vec3 direction{0.0f, -1.0f, 0.0f};
	Vec3 color{1.0f, 1.0f, 1.0f};
	float intensity = 1.0f;
	float range = 10.0f;
	float inner_cone = 0.4f;
	float outer_cone = 0.6f;
	bool casts_shadow = false;
};

/**
 * CPU Lambert (and unlit) lighting at a world position + normal.
 */
class LightSet {
public:
	int Add(const Light light);
	void Remove(const int id);
	Light* Get(const int id);
	const Light* Get(const int id) const;
	const std::vector<Light>& All() const { return lights_; }

	/**
	 * Evaluate RGB in [0,1] at a point. shadow = 1 fully lit, 0 fully shadowed.
	 */
	Vec3 Evaluate(const Vec3 position, const Vec3 normal, const float shadow = 1.0f) const;

	/**
	 * Batch Evaluate into out_rgb (count * 3 floats).
	 */
	void EvaluateBatch(
		const Vec3* positions,
		const Vec3* normals,
		const std::size_t count,
		const float* shadows,
		float* out_rgb) const;

private:
	std::vector<Light> lights_{};
};

/**
 * Directional shadow map: a CPU depth buffer rendered from the light's view.
 *
 * Rasterization still goes through Hyperlite (no graphics API). Sample in lighting.
 */
class ShadowMap {
public:
	void Resize(const int width, const int height);
	int Width() const { return width_; }
	int Height() const { return height_; }

	FrameBuffer& Color() { return color_; }
	DepthBuffer& Depth() { return depth_; }
	Camera& LightCamera() { return camera_; }

	/**
	 * Build an orthographic light camera covering an AABB from a direction.
	 */
	void SetupDirectional(const Vec3 direction, const Aabb world_bounds, const float padding = 1.0f);

	/**
	 * Sample shadow (0 = occluded, 1 = lit) at a world point.
	 */
	float Sample(const Vec3 world, const float bias = 0.002f) const;

	Mat4 LightViewProj() const { return camera_.ViewProj(); }

private:
	int width_ = 0;
	int height_ = 0;
	FrameBuffer color_{};
	DepthBuffer depth_{};
	Camera camera_{};
};

} // namespace hyperlite
