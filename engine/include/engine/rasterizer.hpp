#pragma once

#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"

namespace hyperlite::raster {

/**
 * Execute all queued CPU commands into the target framebuffer.
 */
void ExecuteCommandBuffer(const CommandBuffer& command_buffer, FrameBuffer& framebuffer);

} // namespace hyperlite::raster
