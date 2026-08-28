#include "engine/runtime/culling.hpp"

namespace hyperlite {

std::size_t Culler::CullFrustum(
	const CullItem* items,
	const std::size_t count,
	const Frustum& frustum,
	std::uint32_t* out_indices,
	const std::size_t out_cap) const {
	last_visible_ = 0;
	last_culled_ = 0;
	if (items == nullptr || out_indices == nullptr) {
		return 0;
	}
	std::size_t written = 0;
	for (std::size_t i = 0; i < count; ++i) {
		const bool vis = items[i].sphere.radius > 0.0f
			? FrustumSphere(frustum, items[i].sphere)
			: FrustumAabb(frustum, items[i].aabb);
		if (vis) {
			if (written < out_cap) {
				out_indices[written] = static_cast<std::uint32_t>(i);
				++written;
			}
			++last_visible_;
		} else {
			++last_culled_;
		}
	}
	return written;
}

} // namespace hyperlite
