#pragma once

#include <cstdint>

namespace hyperlite {

/**
 * Packed sprite instance for native bulk draw paths.
 */
struct SpriteDrawDesc {
	std::uint32_t atlas_id = 0;
	std::int32_t src_x = 0;
	std::int32_t src_y = 0;
	std::int32_t width = 0;
	std::int32_t height = 0;
	std::int32_t dst_x = 0;
	std::int32_t dst_y = 0;
};

} // namespace hyperlite
