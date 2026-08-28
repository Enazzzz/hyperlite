#pragma once

#include "engine/runtime/math.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * One cullable object (world AABB / sphere + optional mesh handle).
 */
struct CullItem {
	Aabb aabb{};
	Sphere sphere{};
	int mesh_id = -1;
	int material_id = -1;
	std::uint32_t flags = 0;
};

/**
 * Native frustum culling. Writes compact visible indices — no Python loop.
 */
class Culler {
public:
	/**
	 * Frustum-cull items. Returns count of visible indices written to out_indices.
	 */
	std::size_t CullFrustum(
		const CullItem* items,
		const std::size_t count,
		const Frustum& frustum,
		std::uint32_t* out_indices,
		const std::size_t out_cap) const;

	std::size_t LastVisible() const { return last_visible_; }
	std::size_t LastCulled() const { return last_culled_; }

private:
	mutable std::size_t last_visible_ = 0;
	mutable std::size_t last_culled_ = 0;
};

} // namespace hyperlite
