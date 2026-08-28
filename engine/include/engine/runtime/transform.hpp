#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Integer handle for a transform node (invalid = -1).
 */
using TransformId = int;

/**
 * Native transform graph: local TRS, parent/child, dirty world-matrix cache.
 *
 * No Python object per transform. Batch UpdateDirty() when the programmer asks.
 */
class TransformStore {
public:
	/** Allocate a transform. Returns a stable index handle. */
	TransformId Create(const Vec3 position = {}, const Quat rotation = {}, const Vec3 scale = {1.0f, 1.0f, 1.0f});

	/** Destroy a transform (orphans children to world). */
	void Destroy(const TransformId id);

	bool Valid(const TransformId id) const;

	void SetLocalPosition(const TransformId id, const Vec3 p);
	void SetLocalRotation(const TransformId id, const Quat q);
	void SetLocalScale(const TransformId id, const Vec3 s);
	void SetLocal(const TransformId id, const TransformXform xform);

	Vec3 LocalPosition(const TransformId id) const;
	Quat LocalRotation(const TransformId id) const;
	Vec3 LocalScale(const TransformId id) const;

	void SetParent(const TransformId id, const TransformId parent);
	TransformId Parent(const TransformId id) const;

	/** Recompute dirty world matrices (parents before children). */
	void UpdateDirty();

	/** Force-update every node (use after bulk writes). */
	void UpdateAll();

	const Mat4& WorldMatrix(const TransformId id) const;
	Vec3 WorldPosition(const TransformId id) const;

	/** Write N world matrices into out16 (column-major 4x4 each). */
	void BatchWorldMatrices(const TransformId* ids, const std::size_t count, float* out16) const;

	std::size_t Count() const { return nodes_.size(); }

private:
	struct Node {
		TransformXform local{};
		Mat4 world{};
		TransformId parent = -1;
		std::uint32_t generation = 0;
		bool alive = false;
		bool dirty = true;
	};

	void MarkDirty(const TransformId id);
	void UpdateOne(const TransformId id);

	std::vector<Node> nodes_{};
	std::vector<TransformId> free_{};
};

} // namespace hyperlite
