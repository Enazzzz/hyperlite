#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * One keyframed TRS sample.
 */
struct AnimKey {
	float time = 0.0f;
	Vec3 translation{};
	Quat rotation{};
	Vec3 scale{1.0f, 1.0f, 1.0f};
};

/**
 * Per-bone clip channel.
 */
struct AnimChannel {
	int bone = 0;
	std::vector<AnimKey> keys{};
};

/**
 * Animation clip in seconds.
 */
struct AnimClip {
	std::string name{};
	float duration = 1.0f;
	bool loop = true;
	std::vector<AnimChannel> channels{};
};

/**
 * Skeleton: parent indices (-1 = root) and inverse bind matrices.
 */
struct Skeleton {
	std::vector<int> parent{};
	std::vector<Mat4> inverse_bind{};
	std::vector<TransformXform> rest{};
};

/**
 * Evaluated pose (local + global matrices, one per bone).
 */
struct Pose {
	std::vector<TransformXform> local{};
	std::vector<Mat4> global{};
	std::vector<Mat4> skin{};
};

/**
 * Native clip evaluation, blending, and linear-blend skinning.
 */
class Animator {
public:
	int LoadSkeleton(const Skeleton& skel);
	int LoadClip(const AnimClip& clip);
	const Skeleton* GetSkeleton(const int id) const;
	const AnimClip* GetClip(const int id) const;

	void Evaluate(const int skeleton_id, const int clip_id, const float time, Pose& out) const;
	void Blend(const Pose& a, const Pose& b, const float t, Pose& out) const;

	/**
	 * Skin packed xyz positions with 4 influences (indices + weights per vert).
	 *
	 * out_positions: vertex_count * 3 floats.
	 */
	void Skin(
		const Pose& pose,
		const float* positions,
		const std::uint16_t* joint_indices,
		const float* joint_weights,
		const std::size_t vertex_count,
		float* out_positions) const;

private:
	std::vector<Skeleton> skeletons_{};
	std::vector<AnimClip> clips_{};
};

} // namespace hyperlite
