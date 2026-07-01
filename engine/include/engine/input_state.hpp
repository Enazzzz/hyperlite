#pragma once

#include <array>
#include <cstdint>

#include "engine/types.hpp"

namespace hyperlite {

/**
 * Input state snapshot updated by the platform layer.
 */
struct InputState {
	std::array<bool, 256> key_down{};
	Int2 mouse_pos{0, 0};
	bool quit_requested = false;
};

} // namespace hyperlite
