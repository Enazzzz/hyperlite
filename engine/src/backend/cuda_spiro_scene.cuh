#pragma once

#include <cstdint>

#include <cuda_runtime.h>

namespace hyperlite::cuda_detail {

/**
 * Generate and rasterize the spiro benchmark scene directly into the device
 * framebuffer. One device thread owns one line segment: it computes both
 * endpoints from closed-form trigonometry and draws the connecting line, so
 * geometry generation and rasterization are fused into a single launch.
 *
 * Returns the number of line segments dispatched (instances * segments).
 */
int LaunchSpiroScene(
	std::uint32_t* d_framebuffer,
	int width,
	int height,
	int instances,
	int segments,
	double phase,
	double dt,
	cudaStream_t stream);

/**
 * Same as LaunchSpiroScene but reads phase (params[0]) and dt (params[1]) from a
 * device buffer, enabling CUDA Graph replay with refreshed animation params.
 */
int LaunchSpiroSceneDev(
	std::uint32_t* d_framebuffer,
	int width,
	int height,
	int instances,
	int segments,
	const double* d_params,
	cudaStream_t stream);

} // namespace hyperlite::cuda_detail
