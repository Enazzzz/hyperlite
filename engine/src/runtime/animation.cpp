#include "engine/runtime/animation.hpp"

#include <algorithm>
#include <cmath>

namespace hyperlite {

int Animator::LoadSkeleton(const Skeleton& skel) {
	skeletons_.push_back(skel);
	return static_cast<int>(skeletons_.size()) - 1;
}

int Animator::LoadClip(const AnimClip& clip) {
	clips_.push_back(clip);
	return static_cast<int>(clips_.size()) - 1;
}

const Skeleton* Animator::GetSkeleton(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= skeletons_.size()) {
		return nullptr;
	}
	return &skeletons_[static_cast<std::size_t>(id)];
}

const AnimClip* Animator::GetClip(const int id) const {
	if (id < 0 || static_cast<std::size_t>(id) >= clips_.size()) {
		return nullptr;
	}
	return &clips_[static_cast<std::size_t>(id)];
}

void Animator::Evaluate(const int skeleton_id, const int clip_id, const float time, Pose& out) const {
	const Skeleton* sk = GetSkeleton(skeleton_id);
	const AnimClip* clip = GetClip(clip_id);
	if (sk == nullptr) {
		return;
	}
	const int bones = static_cast<int>(sk->parent.size());
	out.local.resize(static_cast<std::size_t>(bones));
	out.global.resize(static_cast<std::size_t>(bones));
	out.skin.resize(static_cast<std::size_t>(bones));
	for (int i = 0; i < bones; ++i) {
		out.local[static_cast<std::size_t>(i)] = i < static_cast<int>(sk->rest.size()) ? sk->rest[static_cast<std::size_t>(i)] : TransformXform{};
	}
	if (clip != nullptr && clip->duration > 0.0f) {
		float t = time;
		if (clip->loop) {
			t = std::fmod(t, clip->duration);
			if (t < 0.0f) {
				t += clip->duration;
			}
		}
		for (const AnimChannel& ch : clip->channels) {
			if (ch.bone < 0 || ch.bone >= bones || ch.keys.empty()) {
				continue;
			}
			const AnimKey* a = &ch.keys.front();
			const AnimKey* b = &ch.keys.back();
			for (std::size_t k = 0; k + 1 < ch.keys.size(); ++k) {
				if (t >= ch.keys[k].time && t <= ch.keys[k + 1].time) {
					a = &ch.keys[k];
					b = &ch.keys[k + 1];
					break;
				}
			}
			const float span = std::max(1.0e-6f, b->time - a->time);
			const float u = Clamp((t - a->time) / span, 0.0f, 1.0f);
			TransformXform xf{};
			xf.position = Lerp(a->translation, b->translation, u);
			xf.rotation = Nlerp(a->rotation, b->rotation, u);
			xf.scale = Lerp(a->scale, b->scale, u);
			out.local[static_cast<std::size_t>(ch.bone)] = xf;
		}
	}
	for (int i = 0; i < bones; ++i) {
		const Mat4 local = Mat4FromTransform(out.local[static_cast<std::size_t>(i)]);
		const int p = sk->parent[static_cast<std::size_t>(i)];
		if (p >= 0 && p < bones) {
			out.global[static_cast<std::size_t>(i)] = Mul(out.global[static_cast<std::size_t>(p)], local);
		} else {
			out.global[static_cast<std::size_t>(i)] = local;
		}
		if (i < static_cast<int>(sk->inverse_bind.size())) {
			out.skin[static_cast<std::size_t>(i)] = Mul(out.global[static_cast<std::size_t>(i)], sk->inverse_bind[static_cast<std::size_t>(i)]);
		} else {
			out.skin[static_cast<std::size_t>(i)] = out.global[static_cast<std::size_t>(i)];
		}
	}
}

void Animator::Blend(const Pose& a, const Pose& b, const float t, Pose& out) const {
	const std::size_t n = std::min(a.local.size(), b.local.size());
	out.local.resize(n);
	out.global.resize(n);
	out.skin.resize(n);
	for (std::size_t i = 0; i < n; ++i) {
		out.local[i].position = Lerp(a.local[i].position, b.local[i].position, t);
		out.local[i].rotation = Nlerp(a.local[i].rotation, b.local[i].rotation, t);
		out.local[i].scale = Lerp(a.local[i].scale, b.local[i].scale, t);
		out.global[i] = Mat4FromTransform(out.local[i]);
		out.skin[i] = out.global[i];
	}
}

void Animator::Skin(
	const Pose& pose,
	const float* positions,
	const std::uint16_t* joint_indices,
	const float* joint_weights,
	const std::size_t vertex_count,
	float* out_positions) const {
	if (positions == nullptr || joint_indices == nullptr || joint_weights == nullptr || out_positions == nullptr) {
		return;
	}
	const int bones = static_cast<int>(pose.skin.size());
	for (std::size_t v = 0; v < vertex_count; ++v) {
		Vec3 p{positions[v * 3U], positions[v * 3U + 1U], positions[v * 3U + 2U]};
		Vec3 acc{};
		for (int k = 0; k < 4; ++k) {
			const int j = static_cast<int>(joint_indices[v * 4U + static_cast<std::size_t>(k)]);
			const float w = joint_weights[v * 4U + static_cast<std::size_t>(k)];
			if (w <= 0.0f || j < 0 || j >= bones) {
				continue;
			}
			acc = acc + TransformPoint(pose.skin[static_cast<std::size_t>(j)], p) * w;
		}
		out_positions[v * 3U] = acc.x;
		out_positions[v * 3U + 1U] = acc.y;
		out_positions[v * 3U + 2U] = acc.z;
	}
}

} // namespace hyperlite
