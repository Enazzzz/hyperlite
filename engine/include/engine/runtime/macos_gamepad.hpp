#pragma once

#include "engine/runtime/input_runtime.hpp"

namespace hyperlite {

/**
 * Poll connected GameController.framework pads into `pads[0..count)`.
 *
 * No-op when the GameController runtime has no controllers. Safe to call every
 * frame from the game thread (typically main).
 */
void PollMacosGamepads(GamepadState* pads, int count);

} // namespace hyperlite
