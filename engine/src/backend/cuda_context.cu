#include "engine/cuda/cuda_context.hpp"

#include <cstdio>
#include <cstring>

#include <cuda_runtime.h>

#include "cuda_raster_device.cuh"
#include "cuda_spiro_scene.cuh"

namespace hyperlite {

namespace {

using cuda_detail::DeviceDrawLine;
using cuda_detail::DevicePutPixel;

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
 * Write a single clipped pixel.
 */
__global__ void PutPixelKernel(std::uint32_t* fb, const int x, const int y, const int width, const int height, const std::uint32_t packed_color) {
	DevicePutPixel(fb, x, y, width, height, packed_color);
}

/**
 * Draw one Bresenham line (single thread to match CPU output exactly).
 */
__global__ void LineKernel(std::uint32_t* fb, const int x0, const int y0, const int x1, const int y1, const int width, const int height, const std::uint32_t packed_color) {
	DeviceDrawLine(fb, x0, y0, x1, y1, width, height, packed_color);
}

/**
 * Fill an axis-aligned rectangle, clipped to the framebuffer.
 */
__global__ void RectFillKernel(std::uint32_t* fb, const int x0, const int y0, const int rect_w, const int rect_h, const int width, const std::uint32_t packed_color) {
	const int lx = blockIdx.x * blockDim.x + threadIdx.x;
	const int ly = blockIdx.y * blockDim.y + threadIdx.y;
	if (lx >= rect_w || ly >= rect_h) {
		return;
	}
	const int px = x0 + lx;
	const int py = y0 + ly;
	fb[static_cast<std::size_t>(py) * static_cast<std::size_t>(width) + static_cast<std::size_t>(px)] = packed_color;
}

/**
 * Draw a rectangle border as four lines (single thread, matches CPU order).
 */
__global__ void RectOutlineKernel(std::uint32_t* fb, const int x, const int y, const int w, const int h, const int width, const int height, const std::uint32_t packed_color) {
	DeviceDrawLine(fb, x, y, x + w - 1, y, width, height, packed_color);
	DeviceDrawLine(fb, x, y, x, y + h - 1, width, height, packed_color);
	DeviceDrawLine(fb, x + w - 1, y, x + w - 1, y + h - 1, width, height, packed_color);
	DeviceDrawLine(fb, x, y + h - 1, x + w - 1, y + h - 1, width, height, packed_color);
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
	if (!CudaOk(cudaEventCreate(&e0), "cudaEventCreate(frame_start)") ||
		!CudaOk(cudaEventCreate(&e1), "cudaEventCreate(after_kernel)") ||
		!CudaOk(cudaEventCreate(&e2), "cudaEventCreate(after_readback)")) {
		return;
	}
	ev_frame_start_ = static_cast<void*>(e0);
	ev_after_kernel_ = static_cast<void*>(e1);
	ev_after_readback_ = static_cast<void*>(e2);

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

void CudaContext::RenderCommands(const DrawCommand* commands, const std::size_t count) {
	std::uint32_t* fb = ActiveDeviceFramebuffer();
	if (!valid_ || fb == nullptr || commands == nullptr) {
		return;
	}
	MarkFrameStart();

	const auto stream = static_cast<cudaStream_t>(stream_);
	const int width = width_;
	const int height = height_;
	const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

	// Commands are dispatched sequentially on a single stream so painter's
	// order is preserved across overlapping primitives, matching the CPU path.
	for (std::size_t i = 0; i < count; ++i) {
		const DrawCommand& cmd = commands[i];
		switch (cmd.type) {
		case CommandType::kClear: {
			constexpr int block = 256;
			const int blocks = static_cast<int>((pixel_count + block - 1) / block);
			ClearKernel<<<blocks, block, 0, stream>>>(fb, pixel_count, cmd.packed_color);
			break;
		}
		case CommandType::kPutPixel:
			PutPixelKernel<<<1, 1, 0, stream>>>(fb, cmd.x0, cmd.y0, width, height, cmd.packed_color);
			break;
		case CommandType::kLine:
			LineKernel<<<1, 1, 0, stream>>>(fb, cmd.x0, cmd.y0, cmd.x1, cmd.y1, width, height, cmd.packed_color);
			break;
		case CommandType::kRectFill: {
			const int rect_w = cmd.x1;
			const int rect_h = cmd.y1;
			if (rect_w <= 0 || rect_h <= 0) {
				break;
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
				break;
			}
			const int clipped_w = cx1 - cx0;
			const int clipped_h = cy1 - cy0;
			const dim3 block(16, 16);
			const dim3 blocks(
				static_cast<unsigned int>((clipped_w + 15) / 16),
				static_cast<unsigned int>((clipped_h + 15) / 16));
			RectFillKernel<<<blocks, block, 0, stream>>>(fb, cx0, cy0, clipped_w, clipped_h, width, cmd.packed_color);
			break;
		}
		case CommandType::kRectOutline: {
			const int rect_w = cmd.x1;
			const int rect_h = cmd.y1;
			if (rect_w <= 0 || rect_h <= 0) {
				break;
			}
			RectOutlineKernel<<<1, 1, 0, stream>>>(fb, cmd.x0, cmd.y0, rect_w, rect_h, width, height, cmd.packed_color);
			break;
		}
		}
	}
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

	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_kernel_), stream);
	cudaMemcpyAsync(dst, fb, bytes, cudaMemcpyDeviceToHost, stream);
	cudaEventRecord(static_cast<cudaEvent_t>(ev_after_readback_), stream);

	if (!CudaOk(cudaEventSynchronize(static_cast<cudaEvent_t>(ev_after_readback_)), "cudaEventSynchronize(readback)")) {
		frame_started_ = false;
		return;
	}

	float kernel_ms = 0.0f;
	float readback_ms = 0.0f;
	if (frame_started_) {
		cudaEventElapsedTime(&kernel_ms, static_cast<cudaEvent_t>(ev_frame_start_), static_cast<cudaEvent_t>(ev_after_kernel_));
		cudaEventElapsedTime(&readback_ms, static_cast<cudaEvent_t>(ev_after_kernel_), static_cast<cudaEvent_t>(ev_after_readback_));
	}
	last_timings_.upload_ms = 0.0f;
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

} // namespace hyperlite
