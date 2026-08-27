#pragma once

#include "engine/atlas_store.hpp"
#include "engine/command_buffer.hpp"
#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"

namespace hyperlite::raster {

/**
 * Execute all queued CPU commands into the target framebuffer.
 *
 * When depth is non-null and allocated, kClear also resets the depth plane to 1.0.
 * 2D primitives never read or write depth.
 */
void ExecuteCommandBuffer(
	const CommandBuffer& command_buffer,
	FrameBuffer& framebuffer,
	const AtlasStore& atlases,
	DepthBuffer* depth = nullptr);

/**
 * Clear the framebuffer and raster packed int32 line segments [x0,y0,x1,y1,...].
 */
void RasterWireframeSegments(
	FrameBuffer& framebuffer,
	std::uint32_t clear_color,
	const std::int32_t* segments,
	std::size_t line_count,
	std::uint32_t line_color,
	int line_width);

} // namespace hyperlite::raster
