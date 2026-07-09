#include "engine/cuda/cuda_context.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <cuda_runtime.h>

#include "engine/atlas_store.hpp"
#ifdef _WIN32
#include "engine/dxgi_presenter.hpp"
#endif
#include "cuda_raster_device.cuh"
#include "cuda_spiro_scene.cuh"

namespace hyperlite {

namespace {

using cuda_detail::DeviceDrawLine;
using cuda_detail::DeviceDrawLineWithWidth;
using cuda_detail::DevicePutPixel;
using cuda_detail::DeviceRectFill;
using cuda_detail::DeviceStorePixel;

/**
 * Log and swallow a CUDA error, returning whether the call succeeded.
 */
inline bool CudaOk(const cudaError_t status, const char* what) {
	if (status != cudaSuccess) {
		std::fprintf(stderr, "[hyperlite][cuda] %s failed: %s\n", what, cudaGetErrorString(status));
		return false;
	}
	return true;
}

/**
 * Fill the entire device framebuffer with a packed color.
 */
__global__ void ClearKernel(std::uint32_t* fb, const std::size_t pixel_count, const std::uint32_t packed_color) {
	const std::size_t i = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	if (i < pixel_count) {
		fb[i] = packed_color;
	}
}

/**
 * Copy a clipped RGBA image into the destination framebuffer.
 */
__global__ void BlitRgbaKernel(
	const std::uint32_t* src,
	const int src_width,
	const int src_height,
	std::uint32_t* dst,
	const int dst_width,
	const int dst_height,
	const int dst_x,
	const int dst_y) {
	const int x = static_cast<int>(blockIdx.x) * blockDim.x + static_cast<int>(threadIdx.x);
	const int y = static_cast<int>(blockIdx.y) * blockDim.y + static_cast<int>(threadIdx.y);
	if (x >= src_width || y >= src_height) {
		return;
	}
	const int out_x = dst_x + x;
	const int out_y = dst_y + y;
	if (static_cast<unsigned int>(out_x) >= static_cast<unsigned int>(dst_width) ||
		static_cast<unsigned int>(out_y) >= static_cast<unsigned int>(dst_height)) {
		return;
	}
	const std::size_t src_idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(src_width) + static_cast<std::size_t>(x);
	const std::size_t dst_idx = static_cast<std::size_t>(out_y) * static_cast<std::size_t>(dst_width) + static_cast<std::size_t>(out_x);
	dst[dst_idx] = src[src_idx];
}

/**
 * Scatter many put_pixel commands in parallel (one thread per command).
 */
__global__ void ExecutePutPixelsParallelKernel(
	std::uint32_t* fb,
	const DrawCommand* commands,
	const std::size_t start,
	const std::size_t count,
	const int width,
	const int height) {
	if (fb == nullptr || commands == nullptr) {
		return;
	}
	const std::size_t global = start + static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	if (global >= start + count) {
		return;
	}
	const DrawCommand& cmd = commands[global];
	DevicePutPixel(fb, cmd.x0, cmd.y0, width, height, cmd.packed_color);
}

/**
 * Execute line commands in parallel (one thread per command).
 */
__global__ void ExecuteLinesParallelKernel(
	std::uint32_t* fb,
	const DrawCommand* commands,
	const std::size_t start,
	const std::size_t count,
	const int width,
	const int height) {
	const std::size_t global = start + static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	if (global >= start + count) {
		return;
	}
	const DrawCommand& cmd = commands[global];
	if (cmd.type != CommandType::kLine) {
		return;
	}
	DeviceDrawLineWithWidth(
		fb,
		cmd.x0,
		cmd.y0,
		cmd.x1,
		cmd.y1,
		width,
		height,
		cmd.packed_color,
		static_cast<int>(cmd.line_width));
}

/**
 * Fill one rect per CUDA block (rows parallelized across threads).
 */
__global__ void ExecuteRectFillsParallelKernel(
	std::uint32_t* fb,
	const DrawCommand* commands,
	const std::size_t start,
	const std::size_t count,
	const int width,
	const int height) {
	const std::size_t local = static_cast<std::size_t>(blockIdx.x);
	if (local >= count) {
		return;
	}
	const DrawCommand& cmd = commands[start + local];
	if (cmd.type != CommandType::kRectFill) {
		return;
	}
	const int rect_w = cmd.x1;
	const int rect_h = cmd.y1;
	if (rect_w <= 0 || rect_h <= 0) {
		return;
	}
	int cx0 = cmd.x0 < 0 ? 0 : cmd.x0;
	int cy0 = cmd.y0 < 0 ? 0 : cmd.y0;
	int cx1 = cmd.x0 + rect_w;
	int cy1 = cmd.y0 + rect_h;
	if (cx1 > width) {
		cx1 = width;
	}
	if (cy1 > height) {
		cy1 = height;
	}
	if (cx0 >= cx1 || cy0 >= cy1) {
		return;
	}
	for (int py = cy0 + static_cast<int>(threadIdx.x); py < cy1; py += static_cast<int>(blockDim.x)) {
		const std::size_t row = static_cast<std::size_t>(py) * static_cast<std::size_t>(width);
		for (int px = cx0; px < cx1; ++px) {
			fb[row + static_cast<std::size_t>(px)] = cmd.packed_color;
		}
	}
}

/**
 * Draw one outline per thread (four edge lines each).
 */
__global__ void ExecuteRectOutlinesParallelKernel(
	std::uint32_t* fb,
	const DrawCommand* commands,
	const std::size_t start,
	const std::size_t count,
	const int width,
	const int height) {
	const std::size_t global = start + static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	if (global >= start + count) {
		return;
	}
	const DrawCommand& cmd = commands[global];
	if (cmd.type != CommandType::kRectOutline || cmd.x1 <= 0 || cmd.y1 <= 0) {
		return;
	}
	DeviceDrawLine(fb, cmd.x0, cmd.y0, cmd.x0 + cmd.x1 - 1, cmd.y0, width, height, cmd.packed_color);
	DeviceDrawLine(fb, cmd.x0, cmd.y0, cmd.x0, cmd.y0 + cmd.y1 - 1, width, height, cmd.packed_color);
	DeviceDrawLine(fb, cmd.x0 + cmd.x1 - 1, cmd.y0, cmd.x0 + cmd.x1 - 1, cmd.y0 + cmd.y1 - 1, width, height, cmd.packed_color);
	DeviceDrawLine(fb, cmd.x0, cmd.y0 + cmd.y1 - 1, cmd.x0 + cmd.x1 - 1, cmd.y0 + cmd.y1 - 1, width, height, cmd.packed_color);
}

/**
 * Device-side blit descriptor for batched sprite copies.
 */
struct DeviceBlitDesc {
	int dst_x = 0;
	int dst_y = 0;
	int src_x = 0;
	int src_y = 0;
	int width = 0;
	int height = 0;
	int src_stride = 0;
	int atlas_index = -1;
	std::uint32_t inline_offset = 0;
};

/**
 * Copy many clipped blits in one launch (one block per blit).
 */
__global__ void BatchBlitKernel(
	const DeviceBlitDesc* descs,
	const std::uint32_t* inline_pixels,
	const std::uint32_t* const* atlas_pixels,
	const int count,
	std::uint32_t* fb,
	const int fb_width,
	const int fb_height) {
	const int bi = blockIdx.x;
	if (bi >= count) {
		return;
	}
	const DeviceBlitDesc desc = descs[bi];
	const int sx0 = (0 > -desc.dst_x ? 0 : -desc.dst_x) + desc.src_x;
	const int sy0 = (0 > -desc.dst_y ? 0 : -desc.dst_y) + desc.src_y;
	const int dx0 = desc.dst_x > 0 ? desc.dst_x : 0;
	const int dy0 = desc.dst_y > 0 ? desc.dst_y : 0;
	const int clipped_w = (desc.width - (desc.dst_x < 0 ? -desc.dst_x : 0)) < (fb_width - dx0)
		? (desc.width - (desc.dst_x < 0 ? -desc.dst_x : 0))
		: (fb_width - dx0);
	const int clipped_h = (desc.height - (desc.dst_y < 0 ? -desc.dst_y : 0)) < (fb_height - dy0)
		? (desc.height - (desc.dst_y < 0 ? -desc.dst_y : 0))
		: (fb_height - dy0);
	if (clipped_w <= 0 || clipped_h <= 0) {
		return;
	}
	const std::uint32_t* src_base = nullptr;
	int src_stride = desc.src_stride;
	if (desc.atlas_index >= 0) {
		src_base = atlas_pixels[desc.atlas_index];
	} else {
		src_base = inline_pixels + desc.inline_offset;
	}
	if (src_base == nullptr) {
		return;
	}
	for (int row = static_cast<int>(threadIdx.x); row < clipped_h; row += static_cast<int>(blockDim.x)) {
		const int src_y = sy0 + row;
		const int dst_y = dy0 + row;
		const std::size_t src_row = static_cast<std::size_t>(src_y) * static_cast<std::size_t>(src_stride) + static_cast<std::size_t>(sx0);
		const std::size_t dst_row = static_cast<std::size_t>(dst_y) * static_cast<std::size_t>(fb_width) + static_cast<std::size_t>(dx0);
		for (int col = 0; col < clipped_w; ++col) {
			const std::uint32_t src_px = src_base[src_row + static_cast<std::size_t>(col)];
			const std::uint32_t sa = src_px >> 24U;
			std::uint32_t* dst_px = &fb[dst_row + static_cast<std::size_t>(col)];
			if (sa == 255U) {
				*dst_px = src_px;
			} else if (sa != 0U) {
				DeviceStorePixel(dst_px, src_px);
			}
		}
	}
}

/**
 * Raster packed int32 line segments on device (one thread per segment).
 */
__global__ void TickLinesSegmentKernel(
	std::uint32_t* fb,
	const std::int32_t* segments,
	const std::size_t line_count,
	const int width,
	const int height,
	const std::uint32_t line_color,
	const int line_width) {
	const std::size_t line = static_cast<std::size_t>(blockIdx.x) * blockDim.x + threadIdx.x;
	if (line >= line_count || segments == nullptr) {
		return;
	}
	const std::size_t base = line * 4U;
	DeviceDrawLineWithWidth(
		fb,
		segments[base + 0U],
		segments[base + 1U],
		segments[base + 2U],
		segments[base + 3U],
		width,
		height,
		line_color,
		line_width);
}

} // namespace

bool CudaContext::IsAvailable() {
	int device_count = 0;
	const cudaError_t status = cudaGetDeviceCount(&device_count);
	return status == cudaSuccess && device_count > 0;
}

CudaContext::CudaContext() {
	// Block the host thread during sync instead of spin-waiting at 100% CPU.
	(void)cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);

	cudaStream_t created_stream = nullptr;
	if (!CudaOk(cudaStreamCreateWithFlags(&created_stream, cudaStreamNonBlocking), "cudaStreamCreate")) {
		return;
	}
	stream_ = static_cast<void*>(created_stream);

	cudaStream_t created_copy_stream = nullptr;
	if (!CudaOk(cudaStreamCreateWithFlags(&created_copy_stream, cudaStreamNonBlocking), "cudaStreamCreate(copy)")) {
		return;
	}
	copy_stream_ = static_cast<void*>(created_copy_stream);

	cudaEvent_t e0 = nullptr;
	cudaEvent_t e1 = nullptr;
	cudaEvent_t e2 = nullptr;
	cudaEvent_t e3 = nullptr;
	if (!CudaOk(cudaEventCreate(&e0), "cudaEventCreate(frame_start)") ||
		!CudaOk(cudaEventCreate(&e1), "cudaEventCreate(after_upload)") ||
		!CudaOk(cudaEventCreate(&e2), "cudaEventCreate(after_kernel)") ||
		!CudaOk(cudaEventCreate(&e3), "cudaEventCreate(after_readback)")) {
		return;
	}
	ev_frame_start_ = static_cast<void*>(e0);
	ev_after_upload_ = static_cast<void*>(e1);
	ev_after_kernel_ = static_cast<void*>(e2);
	ev_after_readback_ = static_cast<void*>(e3);

	valid_ = true;
}

CudaContext::~CudaContext() {
	for (int i = 0; i < 2; ++i) {
		if (h_present_[i] != nullptr) {
			cudaFreeHost(h_present_[i]);
			h_present_[i] = nullptr;
		}
		if (ev_p_kstart_[i] != nullptr) {
			cudaEventDestroy(static_cast<cudaEvent_t>(ev_p_kstart_[i]));
		}
		if (ev_p_kdone_[i] != nullptr) {
			cudaEventDestroy(static_cast<cudaEvent_t>(ev_p_kdone_[i]));
		}
		if (ev_p_d2h_[i] != nullptr) {
			cudaEventDestroy(static_cast<cudaEvent_t>(ev_p_d2h_[i]));
		}
	}
	if (graph_exec_ != nullptr) {
		cudaGraphExecDestroy(static_cast<cudaGraphExec_t>(graph_exec_));
		graph_exec_ = nullptr;
	}
	if (d_params_ != nullptr) {
		cudaFree(d_params_);
		d_params_ = nullptr;
	}
	if (h_params_ != nullptr) {
		cudaFreeHost(h_params_);
		h_params_ = nullptr;
	}
	if (h_commands_staging_ != nullptr) {
		cudaFreeHost(h_commands_staging_);
		h_commands_staging_ = nullptr;
	}
	if (d_commands_ != nullptr) {
		cudaFree(d_commands_);
		d_commands_ = nullptr;
	}
	if (h_segments_staging_ != nullptr) {
		cudaFreeHost(h_segments_staging_);
		h_segments_staging_ = nullptr;
	}
	if (d_segments_ != nullptr) {
		cudaFree(d_segments_);
		d_segments_ = nullptr;
	}
	if (h_blit_staging_ != nullptr) {
		cudaFreeHost(h_blit_staging_);
		h_blit_staging_ = nullptr;
	}
	if (d_blit_staging_ != nullptr) {
		cudaFree(d_blit_staging_);
		d_blit_staging_ = nullptr;
	}
	if (d_batch_blit_pixels_ != nullptr) {
		cudaFree(d_batch_blit_pixels_);
		d_batch_blit_pixels_ = nullptr;
	}
	if (h_blit_descs_ != nullptr) {
		cudaFreeHost(h_blit_descs_);
		h_blit_descs_ = nullptr;
	}
	if (d_blit_descs_ != nullptr) {
		cudaFree(d_blit_descs_);
		d_blit_descs_ = nullptr;
	}
	if (d_atlas_table_ != nullptr) {
		cudaFree(d_atlas_table_);
		d_atlas_table_ = nullptr;
	}
	for (DeviceAtlas& atlas : device_atlases_) {
		if (atlas.pixels != nullptr) {
			cudaFree(atlas.pixels);
			atlas.pixels = nullptr;
		}
	}
	device_atlases_.clear();
	if (registered_host_ptr_ != nullptr) {
		cudaHostUnregister(registered_host_ptr_);
		registered_host_ptr_ = nullptr;
	}
	for (int i = 0; i < 2; ++i) {
		if (d_framebuffer_[i] != nullptr) {
			cudaFree(d_framebuffer_[i]);
			d_framebuffer_[i] = nullptr;
		}
	}
	if (ev_frame_start_ != nullptr) {
		cudaEventDestroy(static_cast<cudaEvent_t>(ev_frame_start_));
	}
	if (ev_after_upload_ != nullptr) {
		cudaEventDestroy(static_cast<cudaEvent_t>(ev_after_upload_));
	}
	if (ev_after_kernel_ != nullptr) {
		cudaEventDestroy(static_cast<cudaEvent_t>(ev_after_kernel_));
	}
	if (ev_after_readback_ != nullptr) {
		cudaEventDestroy(static_cast<cudaEvent_t>(ev_after_readback_));
	}
	if (copy_stream_ != nullptr) {
		cudaStreamDestroy(static_cast<cudaStream_t>(copy_stream_));
	}
	if (stream_ != nullptr) {
		cudaStreamDestroy(static_cast<cudaStream_t>(stream_));
	}
}

void CudaContext::EnsureSized(const int width, const int height) {
	if (!valid_ || width <= 0 || height <= 0) {
		return;
	}
	width_ = width;
	height_ = height;

	const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

	if (pixel_count > d_framebuffer_capacity_) {
		for (int i = 0; i < 2; ++i) {
			if (d_framebuffer_[i] != nullptr) {
				cudaFree(d_framebuffer_[i]);
				d_framebuffer_[i] = nullptr;
			}
			if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_framebuffer_[i]), pixel_count * sizeof(std::uint32_t)), "cudaMalloc(framebuffer)")) {
				d_framebuffer_capacity_ = 0;
				valid_ = false;
				return;
			}
		}
		d_framebuffer_capacity_ = pixel_count;
		if (graph_exec_ != nullptr) {
			cudaGraphExecDestroy(static_cast<cudaGraphExec_t>(graph_exec_));
			graph_exec_ = nullptr;
			has_graph_ = false;
		}
	}
}

bool CudaContext::EnsureCommandCapacity(const std::size_t count) {
	if (!valid_ || count == 0 || count <= commands_capacity_) {
		return valid_;
	}
	const std::size_t new_capacity = std::max(count, commands_capacity_ == 0 ? (1U << 21U) : commands_capacity_ * 2U);
	if (d_commands_ != nullptr) {
		cudaFree(d_commands_);
		d_commands_ = nullptr;
	}
	if (h_commands_staging_ != nullptr) {
		cudaFreeHost(h_commands_staging_);
		h_commands_staging_ = nullptr;
	}
	const std::size_t bytes = new_capacity * sizeof(DrawCommand);
	if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_commands_), bytes), "cudaMalloc(commands)")) {
		commands_capacity_ = 0;
		valid_ = false;
		return false;
	}
	if (!CudaOk(cudaHostAlloc(reinterpret_cast<void**>(&h_commands_staging_), bytes, cudaHostAllocDefault), "cudaHostAlloc(commands)")) {
		commands_capacity_ = 0;
		valid_ = false;
		return false;
	}
	commands_capacity_ = new_capacity;
	return true;
}

bool CudaContext::EnsureSegmentCapacity(const std::size_t count) {
	if (!valid_ || count == 0 || count <= segments_capacity_) {
		return valid_;
	}
	const std::size_t new_capacity = std::max(count, segments_capacity_ == 0 ? (1U << 18U) : segments_capacity_ * 2U);
	if (d_segments_ != nullptr) {
		cudaFree(d_segments_);
		d_segments_ = nullptr;
	}
	if (h_segments_staging_ != nullptr) {
		cudaFreeHost(h_segments_staging_);
		h_segments_staging_ = nullptr;
	}
	const std::size_t bytes = new_capacity * sizeof(std::int32_t);
	if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_segments_), bytes), "cudaMalloc(segments)")) {
		segments_capacity_ = 0;
		valid_ = false;
		return false;
	}
	if (!CudaOk(cudaHostAlloc(reinterpret_cast<void**>(&h_segments_staging_), bytes, cudaHostAllocDefault), "cudaHostAlloc(segments)")) {
		segments_capacity_ = 0;
		valid_ = false;
		return false;
	}
	segments_capacity_ = new_capacity;
	return true;
}

std::uint32_t* CudaContext::ActiveDeviceFramebuffer() const {
	if (pipelined_) {
		return d_framebuffer_[static_cast<int>(pframe_ & 1U)];
	}
	return d_framebuffer_[0];
}

void CudaContext::MarkFrameStart() {
	if (frame_started_) {
		return;
	}
	const auto stream = static_cast<cudaStream_t>(stream_);
	if (pipelined_ && ev_p_kstart_[pframe_ & 1U] != nullptr) {
		cudaEventRecord(static_cast<cudaEvent_t>(ev_p_kstart_[pframe_ & 1U]), stream);
	} else {
		cudaEventRecord(static_cast<cudaEvent_t>(ev_frame_start_), stream);
	}
	frame_started_ = true;
}

void CudaContext::ClearDevice(const std::uint32_t packed_color) {
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr) {
		return;
	}
	MarkFrameStart();
	const std::size_t pixel_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
	constexpr int block = 256;
	const int blocks = static_cast<int>((pixel_count + block - 1) / block);
	ClearKernel<<<blocks, block, 0, static_cast<cudaStream_t>(stream_)>>>(fb, pixel_count, packed_color);
}

void CudaContext::UploadFramebuffer(const std::uint8_t* src, const std::size_t bytes) {
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr || src == nullptr) {
		return;
	}
	const std::size_t required = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * sizeof(std::uint32_t);
	if (bytes != required) {
		return;
	}
	MarkFrameStart();
	const auto stream = static_cast<cudaStream_t>(stream_);
	cudaMemcpyAsync(fb, src, bytes, cudaMemcpyHostToDevice, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_upload_), stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
}

void CudaContext::BlitRgba(
	const std::uint8_t* src,
	const std::size_t bytes,
	const int dst_x,
	const int dst_y,
	const int width,
	const int height) {
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr || src == nullptr || width <= 0 || height <= 0) {
		return;
	}
	const std::size_t required = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
	if (bytes < required) {
		return;
	}
	const int src_x0 = std::max(0, -dst_x);
	const int src_y0 = std::max(0, -dst_y);
	const int clipped_x = std::max(0, dst_x);
	const int clipped_y = std::max(0, dst_y);
	const int clipped_w = std::min(width - src_x0, width_ - clipped_x);
	const int clipped_h = std::min(height - src_y0, height_ - clipped_y);
	if (clipped_w <= 0 || clipped_h <= 0) {
		return;
	}

	const std::size_t clipped_bytes = static_cast<std::size_t>(clipped_w) * static_cast<std::size_t>(clipped_h) * 4U;
	if (clipped_bytes > blit_capacity_bytes_) {
		if (h_blit_staging_ != nullptr) {
			cudaFreeHost(h_blit_staging_);
			h_blit_staging_ = nullptr;
		}
		if (d_blit_staging_ != nullptr) {
			cudaFree(d_blit_staging_);
			d_blit_staging_ = nullptr;
		}
		if (!CudaOk(cudaHostAlloc(reinterpret_cast<void**>(&h_blit_staging_), clipped_bytes, cudaHostAllocDefault), "cudaHostAlloc(blit_staging)")) {
			valid_ = false;
			blit_capacity_bytes_ = 0;
			return;
		}
		if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_blit_staging_), clipped_bytes), "cudaMalloc(blit_staging)")) {
			valid_ = false;
			blit_capacity_bytes_ = 0;
			return;
		}
		blit_capacity_bytes_ = clipped_bytes;
	}

	const std::size_t src_stride = static_cast<std::size_t>(width) * 4U;
	const std::size_t copy_stride = static_cast<std::size_t>(clipped_w) * 4U;
	for (int row = 0; row < clipped_h; ++row) {
		const auto* src_row = src + (static_cast<std::size_t>(src_y0 + row) * src_stride) + (static_cast<std::size_t>(src_x0) * 4U);
		auto* dst_row = h_blit_staging_ + static_cast<std::size_t>(row) * copy_stride;
		std::memcpy(dst_row, src_row, copy_stride);
	}

	MarkFrameStart();
	const auto stream = static_cast<cudaStream_t>(stream_);
	cudaMemcpyAsync(d_blit_staging_, h_blit_staging_, clipped_bytes, cudaMemcpyHostToDevice, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_upload_), stream);
	const dim3 block(16, 16);
	const dim3 grid(
		static_cast<unsigned int>((clipped_w + 15) / 16),
		static_cast<unsigned int>((clipped_h + 15) / 16));
	BlitRgbaKernel<<<grid, block, 0, stream>>>(
		reinterpret_cast<const std::uint32_t*>(d_blit_staging_),
		clipped_w,
		clipped_h,
		fb,
		width_,
		height_,
		clipped_x,
		clipped_y);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
}

void CudaContext::EnsureAtlasResident(const int handle, const std::uint8_t* src, const std::size_t bytes, const int width, const int height) {
	if (!valid_ || src == nullptr || handle < 0 || width <= 0 || height <= 0) {
		return;
	}
	const std::size_t required_pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	const std::size_t required_bytes = required_pixels * 4U;
	if (bytes < required_bytes) {
		return;
	}
	if (static_cast<std::size_t>(handle) >= device_atlases_.size()) {
		device_atlases_.resize(static_cast<std::size_t>(handle) + 1U);
	}
	DeviceAtlas& atlas = device_atlases_[static_cast<std::size_t>(handle)];
	if (atlas.uploaded_ && atlas.width == width && atlas.height == height && atlas.pixel_count == required_pixels) {
		return;
	}
	if (atlas.pixels != nullptr && atlas.pixel_count != required_pixels) {
		cudaFree(atlas.pixels);
		atlas.pixels = nullptr;
		atlas.pixel_count = 0;
		atlas.uploaded_ = false;
	}
	if (atlas.pixels == nullptr) {
		if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&atlas.pixels), required_bytes), "cudaMalloc(atlas)")) {
			valid_ = false;
			return;
		}
	}
	atlas.width = width;
	atlas.height = height;
	atlas.pixel_count = required_pixels;
	const auto stream = static_cast<cudaStream_t>(stream_);
	cudaMemcpyAsync(atlas.pixels, src, required_bytes, cudaMemcpyHostToDevice, stream);
	atlas.uploaded_ = true;
}

void CudaContext::RenderFrame(const CommandBuffer& batch, const AtlasStore& atlases) {
	const DrawCommand* commands = batch.Data();
	const std::size_t count = batch.Size();
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr || count == 0) {
		return;
	}
	if (commands == nullptr || !EnsureCommandCapacity(count)) {
		return;
	}

	MarkFrameStart();
	const auto stream = static_cast<cudaStream_t>(stream_);
	const std::size_t bytes = count * sizeof(DrawCommand);
	std::memcpy(h_commands_staging_, commands, bytes);
	cudaMemcpyAsync(d_commands_, h_commands_staging_, bytes, cudaMemcpyHostToDevice, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_upload_), stream);

	constexpr int block = 256;
	const std::size_t pixel_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
	const auto& blit_records = batch.Blits().Records();
	std::vector<std::uint32_t> inline_blit_pixels{};
	std::vector<DeviceBlitDesc> pending_descs{};

	const auto flush_batch_blits = [&]() {
		if (pending_descs.empty()) {
			return;
		}
		const std::size_t desc_count = pending_descs.size();
		const std::size_t desc_bytes = desc_count * sizeof(DeviceBlitDesc);
		if (desc_count > blit_desc_capacity_) {
			if (h_blit_descs_ != nullptr) {
				cudaFreeHost(h_blit_descs_);
				h_blit_descs_ = nullptr;
			}
			if (d_blit_descs_ != nullptr) {
				cudaFree(d_blit_descs_);
				d_blit_descs_ = nullptr;
			}
			if (!CudaOk(cudaHostAlloc(&h_blit_descs_, desc_bytes, cudaHostAllocDefault), "cudaHostAlloc(blit_descs)")) {
				valid_ = false;
				pending_descs.clear();
				inline_blit_pixels.clear();
				return;
			}
			if (!CudaOk(cudaMalloc(&d_blit_descs_, desc_bytes), "cudaMalloc(blit_descs)")) {
				valid_ = false;
				pending_descs.clear();
				inline_blit_pixels.clear();
				return;
			}
			blit_desc_capacity_ = desc_count;
		}
		if (!inline_blit_pixels.empty() && inline_blit_pixels.size() > batch_blit_pixel_capacity_) {
			if (d_batch_blit_pixels_ != nullptr) {
				cudaFree(d_batch_blit_pixels_);
				d_batch_blit_pixels_ = nullptr;
			}
			const std::size_t pixel_bytes = inline_blit_pixels.size() * sizeof(std::uint32_t);
			if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_batch_blit_pixels_), pixel_bytes), "cudaMalloc(batch_blit_pixels)")) {
				valid_ = false;
				pending_descs.clear();
				inline_blit_pixels.clear();
				return;
			}
			batch_blit_pixel_capacity_ = inline_blit_pixels.size();
		}
		std::memcpy(h_blit_descs_, pending_descs.data(), desc_bytes);
		cudaMemcpyAsync(d_blit_descs_, h_blit_descs_, desc_bytes, cudaMemcpyHostToDevice, stream);
		if (!inline_blit_pixels.empty()) {
			cudaMemcpyAsync(
				d_batch_blit_pixels_,
				inline_blit_pixels.data(),
				inline_blit_pixels.size() * sizeof(std::uint32_t),
				cudaMemcpyHostToDevice,
				stream);
		}
		if (device_atlases_.size() > atlas_table_capacity_) {
			if (d_atlas_table_ != nullptr) {
				cudaFree(d_atlas_table_);
				d_atlas_table_ = nullptr;
			}
			const std::size_t table_bytes = device_atlases_.size() * sizeof(std::uint32_t*);
			if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_atlas_table_), table_bytes), "cudaMalloc(atlas_table)")) {
				valid_ = false;
				pending_descs.clear();
				inline_blit_pixels.clear();
				return;
			}
			atlas_table_capacity_ = device_atlases_.size();
		}
		std::vector<std::uint32_t*> host_atlas_table(device_atlases_.size(), nullptr);
		for (std::size_t ai = 0; ai < device_atlases_.size(); ++ai) {
			host_atlas_table[ai] = device_atlases_[ai].pixels;
		}
		if (!host_atlas_table.empty()) {
			cudaMemcpyAsync(d_atlas_table_, host_atlas_table.data(), host_atlas_table.size() * sizeof(std::uint32_t*), cudaMemcpyHostToDevice, stream);
		}
		BatchBlitKernel<<<static_cast<int>(desc_count), block, 0, stream>>>(
			reinterpret_cast<const DeviceBlitDesc*>(d_blit_descs_),
			d_batch_blit_pixels_,
			d_atlas_table_,
			static_cast<int>(desc_count),
			fb,
			width_,
			height_);
		pending_descs.clear();
		inline_blit_pixels.clear();
	};

	const auto queue_blit_record = [&](const BlitRecord& record) {
		DeviceBlitDesc desc{};
		desc.dst_x = record.dst_x;
		desc.dst_y = record.dst_y;
		desc.src_x = record.src_x;
		desc.src_y = record.src_y;
		desc.width = record.width;
		desc.height = record.height;
		if (record.kind == BlitRecord::Kind::kInline) {
			desc.src_stride = record.width;
			desc.atlas_index = -1;
			desc.inline_offset = static_cast<std::uint32_t>(inline_blit_pixels.size());
			const std::uint8_t* src = batch.Blits().Data() + record.data_offset;
			const std::size_t pixel_count = static_cast<std::size_t>(record.width) * static_cast<std::size_t>(record.height);
			const auto* src_pixels = reinterpret_cast<const std::uint32_t*>(src);
			inline_blit_pixels.insert(inline_blit_pixels.end(), src_pixels, src_pixels + pixel_count);
		} else {
			desc.atlas_index = static_cast<int>(record.atlas_id);
			desc.inline_offset = 0;
			const AtlasEntry* entry = atlases.Get(static_cast<int>(record.atlas_id));
			desc.src_stride = entry != nullptr ? entry->width : record.width;
		}
		pending_descs.push_back(desc);
	};

	std::size_t i = 0;
	while (i < count) {
		if (commands[i].type == CommandType::kClear) {
			flush_batch_blits();
			const int clear_blocks = static_cast<int>((pixel_count + static_cast<std::size_t>(block) - 1U) / static_cast<std::size_t>(block));
			ClearKernel<<<clear_blocks, block, 0, stream>>>(fb, pixel_count, commands[i].packed_color);
			++i;
			continue;
		}
		if (commands[i].type == CommandType::kUploadFrame) {
			flush_batch_blits();
			if (batch.UploadFrameSize() == pixel_count * 4U) {
				cudaMemcpyAsync(fb, batch.UploadFrameData(), batch.UploadFrameSize(), cudaMemcpyHostToDevice, stream);
			}
			++i;
			continue;
		}
		if (commands[i].type == CommandType::kPutPixel) {
			flush_batch_blits();
			const std::size_t run_start = i;
			while (i < count && commands[i].type == CommandType::kPutPixel) {
				++i;
			}
			const std::size_t run_count = i - run_start;
			const int blocks = static_cast<int>((run_count + static_cast<std::size_t>(block) - 1U) / static_cast<std::size_t>(block));
			ExecutePutPixelsParallelKernel<<<blocks, block, 0, stream>>>(fb, d_commands_, run_start, run_count, width_, height_);
			continue;
		}
		if (commands[i].type == CommandType::kLine) {
			flush_batch_blits();
			const std::size_t run_start = i;
			while (i < count && commands[i].type == CommandType::kLine) {
				++i;
			}
			const std::size_t run_count = i - run_start;
			const int blocks = static_cast<int>((run_count + static_cast<std::size_t>(block) - 1U) / static_cast<std::size_t>(block));
			ExecuteLinesParallelKernel<<<blocks, block, 0, stream>>>(fb, d_commands_, run_start, run_count, width_, height_);
			continue;
		}
		if (commands[i].type == CommandType::kRectFill) {
			flush_batch_blits();
			const std::size_t run_start = i;
			while (i < count && commands[i].type == CommandType::kRectFill) {
				++i;
			}
			const std::size_t run_count = i - run_start;
			ExecuteRectFillsParallelKernel<<<static_cast<int>(run_count), block, 0, stream>>>(fb, d_commands_, run_start, run_count, width_, height_);
			continue;
		}
		if (commands[i].type == CommandType::kRectOutline) {
			flush_batch_blits();
			const std::size_t run_start = i;
			while (i < count && commands[i].type == CommandType::kRectOutline) {
				++i;
			}
			const std::size_t run_count = i - run_start;
			const int blocks = static_cast<int>((run_count + static_cast<std::size_t>(block) - 1U) / static_cast<std::size_t>(block));
			ExecuteRectOutlinesParallelKernel<<<blocks, block, 0, stream>>>(fb, d_commands_, run_start, run_count, width_, height_);
			continue;
		}
		if (commands[i].type == CommandType::kBlit || commands[i].type == CommandType::kDrawSprite) {
			const std::size_t run_start = i;
			while (i < count && (commands[i].type == CommandType::kBlit || commands[i].type == CommandType::kDrawSprite)) {
				const std::uint32_t record_index = commands[i].packed_color;
				if (record_index < blit_records.size()) {
					queue_blit_record(blit_records[record_index]);
				}
				++i;
			}
			flush_batch_blits();
			(void)run_start;
			continue;
		}
		++i;
	}
	flush_batch_blits();
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
}

void CudaContext::RenderCommands(const DrawCommand* commands, const std::size_t count) {
	CommandBuffer temp{};
	if (commands != nullptr && count > 0) {
		temp.PushRange(commands, count);
	}
	AtlasStore empty{};
	RenderFrame(temp, empty);
}

int CudaContext::SpiroScene(const int width, const int height, const int instances, const int segments, const double phase, const double dt) {
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr) {
		return 0;
	}
	MarkFrameStart();
	return cuda_detail::LaunchSpiroScene(
		fb,
		width,
		height,
		instances,
		segments,
		phase,
		dt,
		static_cast<cudaStream_t>(stream_));
}

int CudaContext::SpiroSceneGraphed(const int width, const int height, const int instances, const int segments, const double phase, const double dt, const std::uint32_t clear_packed) {
	if (!valid_ || width <= 0 || height <= 0 || instances <= 0 || segments <= 1) {
		return 0;
	}
	EnsureSized(width, height);
	if (!valid_ || d_framebuffer_[0] == nullptr) {
		return 0;
	}

	const auto stream = static_cast<cudaStream_t>(stream_);

	// Allocate the tiny animation-parameter buffers once.
	if (d_params_ == nullptr) {
		if (!CudaOk(cudaMalloc(reinterpret_cast<void**>(&d_params_), 2U * sizeof(double)), "cudaMalloc(params)")) {
			return 0;
		}
	}
	if (h_params_ == nullptr) {
		if (!CudaOk(cudaHostAlloc(reinterpret_cast<void**>(&h_params_), 2U * sizeof(double), cudaHostAllocDefault), "cudaHostAlloc(params)")) {
			return 0;
		}
	}

	const bool shape_changed =
		!has_graph_ ||
		cap_width_ != width ||
		cap_height_ != height ||
		cap_instances_ != instances ||
		cap_segments_ != segments ||
		cap_clear_packed_ != clear_packed;

	if (shape_changed) {
		if (graph_exec_ != nullptr) {
			cudaGraphExecDestroy(static_cast<cudaGraphExec_t>(graph_exec_));
			graph_exec_ = nullptr;
		}
		has_graph_ = false;

		const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
		constexpr int block = 256;
		const int blocks = static_cast<int>((pixel_count + block - 1) / block);

		cudaGraph_t graph = nullptr;
		if (!CudaOk(cudaStreamBeginCapture(stream, cudaStreamCaptureModeThreadLocal), "cudaStreamBeginCapture")) {
			return 0;
		}
		cudaMemcpyAsync(d_params_, h_params_, 2U * sizeof(double), cudaMemcpyHostToDevice, stream);
		ClearKernel<<<blocks, block, 0, stream>>>(d_framebuffer_[0], pixel_count, clear_packed);
		cuda_detail::LaunchSpiroSceneDev(d_framebuffer_[0], width, height, instances, segments, d_params_, stream);
		if (!CudaOk(cudaStreamEndCapture(stream, &graph), "cudaStreamEndCapture")) {
			return 0;
		}

		cudaGraphExec_t exec = nullptr;
		const bool instantiated = CudaOk(cudaGraphInstantiateWithFlags(&exec, graph, 0), "cudaGraphInstantiate");
		cudaGraphDestroy(graph);
		if (!instantiated) {
			return 0;
		}
		graph_exec_ = static_cast<void*>(exec);
		cap_width_ = width;
		cap_height_ = height;
		cap_instances_ = instances;
		cap_segments_ = segments;
		cap_clear_packed_ = clear_packed;
		has_graph_ = true;
	}

	// Refresh per-frame animation params, then replay the captured sequence.
	h_params_[0] = phase;
	h_params_[1] = dt;
	MarkFrameStart();
	cudaGraphLaunch(static_cast<cudaGraphExec_t>(graph_exec_), stream);

	return instances * segments;
}

int CudaContext::SpiroSceneFrameDirect(
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	const std::uint32_t clear_packed) {
	if (!valid_ || width <= 0 || height <= 0 || instances <= 0 || segments <= 1) {
		return 0;
	}
	EnsureSized(width, height);
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr) {
		return 0;
	}

	MarkFrameStart();
	const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	constexpr int block = 256;
	const int blocks = static_cast<int>((pixel_count + block - 1) / block);
	const auto stream = static_cast<cudaStream_t>(stream_);
	ClearKernel<<<blocks, block, 0, stream>>>(fb, pixel_count, clear_packed);
	return cuda_detail::LaunchSpiroScene(fb, width, height, instances, segments, phase, dt, stream);
}

int CudaContext::TickLinesFrame(
	const std::uint32_t clear_packed,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width) {
	if (!valid_ || width_ <= 0 || height_ <= 0) {
		return 0;
	}
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (fb == nullptr) {
		return 0;
	}

	MarkFrameStart();
	const std::size_t pixel_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
	constexpr int block = 256;
	const int clear_blocks = static_cast<int>((pixel_count + block - 1) / block);
	const auto stream = static_cast<cudaStream_t>(stream_);
	ClearKernel<<<clear_blocks, block, 0, stream>>>(fb, pixel_count, clear_packed);

	if (segments == nullptr || line_count == 0U) {
		cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
		return 0;
	}

	const std::size_t segment_values = line_count * 4U;
	if (!EnsureSegmentCapacity(segment_values)) {
		return 0;
	}
	const std::size_t segment_bytes = segment_values * sizeof(std::int32_t);
	std::memcpy(h_segments_staging_, segments, segment_bytes);
	cudaMemcpyAsync(d_segments_, h_segments_staging_, segment_bytes, cudaMemcpyHostToDevice, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_upload_), stream);

	const int line_blocks = static_cast<int>((line_count + block - 1) / block);
	TickLinesSegmentKernel<<<line_blocks, block, 0, stream>>>(
		fb,
		d_segments_,
		line_count,
		width_,
		height_,
		line_color,
		line_width);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
	return static_cast<int>(line_count);
}

void CudaContext::ReadbackToHost(std::uint8_t* dst, const std::size_t bytes) {
	std::uint32_t* fb = d_framebuffer_[0];
	if (!valid_ || fb == nullptr || dst == nullptr) {
		return;
	}

	const auto stream = static_cast<cudaStream_t>(stream_);

	// Page-lock the present target once so the readback is a direct pinned DMA
	// straight into the framebuffer the Win32 presenter blits from, avoiding an
	// extra host-side staging copy every frame.
	if (dst != registered_host_ptr_ || bytes != registered_host_bytes_) {
		if (registered_host_ptr_ != nullptr) {
			cudaHostUnregister(registered_host_ptr_);
			registered_host_ptr_ = nullptr;
			registered_host_bytes_ = 0;
		}
		if (cudaHostRegister(dst, bytes, cudaHostRegisterDefault) == cudaSuccess) {
			registered_host_ptr_ = dst;
			registered_host_bytes_ = bytes;
		} else {
			cudaGetLastError(); // pageable fallback: clear the sticky error
		}
	}

	cudaMemcpyAsync(dst, fb, bytes, cudaMemcpyDeviceToHost, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_readback_), stream);

	if (!CudaOk(cudaEventSynchronize(static_cast<cudaEvent_t>(ev_after_readback_)), "cudaEventSynchronize(readback)")) {
		frame_started_ = false;
		return;
	}

	float kernel_ms = 0.0f;
	float upload_ms = 0.0f;
	float readback_ms = 0.0f;
	if (frame_started_) {
		cudaEventElapsedTime(&upload_ms, static_cast<cudaEvent_t>(ev_frame_start_), static_cast<cudaEvent_t>(ev_after_upload_));
		cudaEventElapsedTime(&kernel_ms, static_cast<cudaEvent_t>(ev_after_upload_), static_cast<cudaEvent_t>(ev_after_kernel_));
		cudaEventElapsedTime(&readback_ms, static_cast<cudaEvent_t>(ev_after_kernel_), static_cast<cudaEvent_t>(ev_after_readback_));
	}
	last_timings_.upload_ms = upload_ms;
	last_timings_.kernel_ms = kernel_ms;
	last_timings_.readback_ms = readback_ms;

	frame_started_ = false;
}

void CudaContext::SetPipelined(const bool enabled) {
	if (!valid_) {
		return;
	}
	if (enabled && ev_p_kstart_[0] == nullptr) {
		// Create per-buffer timing events up front so MarkFrameStart can route to
		// them from the very first frame.
		bool ok = true;
		for (int i = 0; i < 2; ++i) {
			cudaEvent_t a = nullptr;
			cudaEvent_t b = nullptr;
			cudaEvent_t c = nullptr;
			ok = ok && CudaOk(cudaEventCreate(&a), "cudaEventCreate(p_kstart)");
			ok = ok && CudaOk(cudaEventCreate(&b), "cudaEventCreate(p_kdone)");
			ok = ok && CudaOk(cudaEventCreate(&c), "cudaEventCreate(p_d2h)");
			ev_p_kstart_[i] = static_cast<void*>(a);
			ev_p_kdone_[i] = static_cast<void*>(b);
			ev_p_d2h_[i] = static_cast<void*>(c);
		}
		if (!ok) {
			return;
		}
	}
	pipelined_ = enabled;
	pframe_ = 0;
	present_has_prev_ = false;
	frame_started_ = false;
}

const std::uint8_t* CudaContext::PresentPipelined(const std::size_t bytes) {
	if (!valid_ || d_framebuffer_[0] == nullptr || !pipelined_ || ev_p_kstart_[0] == nullptr) {
		return nullptr;
	}

	const auto compute_stream = static_cast<cudaStream_t>(stream_);
	const auto copy_stream = static_cast<cudaStream_t>(copy_stream_);

	// (Re)allocate the two pinned staging buffers when the frame size changes.
	if (h_present_[0] == nullptr || bytes != present_bytes_) {
		for (int i = 0; i < 2; ++i) {
			if (h_present_[i] != nullptr) {
				cudaFreeHost(h_present_[i]);
				h_present_[i] = nullptr;
			}
			if (!CudaOk(cudaHostAlloc(reinterpret_cast<void**>(&h_present_[i]), bytes, cudaHostAllocDefault), "cudaHostAlloc(present)")) {
				present_bytes_ = 0;
				return nullptr;
			}
		}
		present_bytes_ = bytes;
		present_has_prev_ = false;
	}

	const int cur = static_cast<int>(pframe_ & 1U);
	std::uint32_t* const dev_fb = d_framebuffer_[cur];

	// Record compute completion on the compute stream, then copy on a dedicated
	// stream so the next frame's kernels can overlap this frame's D2H transfer.
	cudaEventRecord(static_cast<cudaEvent_t>(ev_p_kdone_[cur]), compute_stream);
	cudaStreamWaitEvent(copy_stream, static_cast<cudaEvent_t>(ev_p_kdone_[cur]), 0);
	cudaMemcpyAsync(h_present_[cur], dev_fb, bytes, cudaMemcpyDeviceToHost, copy_stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_p_d2h_[cur]), copy_stream);
	frame_started_ = false;

	const std::uint8_t* ready = nullptr;
	if (present_has_prev_) {
		const int prev = static_cast<int>((pframe_ - 1U) & 1U);
		// The previous frame's readback has had a whole iteration to finish, so
		// this wait is typically near-zero; it only enforces correctness.
		cudaEventSynchronize(static_cast<cudaEvent_t>(ev_p_d2h_[prev]));
		float kernel_ms = 0.0f;
		float readback_ms = 0.0f;
		cudaEventElapsedTime(&kernel_ms, static_cast<cudaEvent_t>(ev_p_kstart_[prev]), static_cast<cudaEvent_t>(ev_p_kdone_[prev]));
		cudaEventElapsedTime(&readback_ms, static_cast<cudaEvent_t>(ev_p_kdone_[prev]), static_cast<cudaEvent_t>(ev_p_d2h_[prev]));
		last_timings_.upload_ms = 0.0f;
		last_timings_.kernel_ms = kernel_ms;
		last_timings_.readback_ms = readback_ms;
		ready = h_present_[prev];
	}

	present_has_prev_ = true;
	++pframe_;
	return ready;
}

void CudaContext::BindDxgiPresenter(void* presenter) {
	dxgi_presenter_ = presenter;
}

bool CudaContext::PresentDirect() {
#ifdef _WIN32
	if (!valid_ || dxgi_presenter_ == nullptr) {
		return false;
	}
	std::uint32_t* fb = d_framebuffer_[0];
	if (fb == nullptr) {
		return false;
	}
	const auto stream = static_cast<cudaStream_t>(stream_);
	if (frame_started_) {
		if (!CudaOk(cudaEventSynchronize(static_cast<cudaEvent_t>(ev_after_kernel_)), "cudaEventSynchronize(pre-present)")) {
			return false;
		}
	}
	auto* presenter = static_cast<DxgiPresenter*>(dxgi_presenter_);
	const bool ok = presenter->CopyFromDeviceAndPresent(fb, width_, height_, stream_);
	last_timings_.readback_ms = 0.0f;
	frame_started_ = false;
	return ok;
#else
	return false;
#endif
}

} // namespace hyperlite
