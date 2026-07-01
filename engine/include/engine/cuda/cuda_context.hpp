#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"

namespace hyperlite {

/**
 * Persistent CUDA device state for the GPU-first renderer.
 *
 * Owns device-resident framebuffer storage, a pinned host staging buffer, a
 * single execution stream, and timing events. All CUDA types are hidden behind
 * opaque pointers so this header stays compilable by the plain C++ frontend
 * (the implementation lives in a .cu translation unit).
 */
class CudaContext {
public:
	/**
	 * Return true when at least one CUDA device is present.
	 */
	static bool IsAvailable();

	CudaContext();
	~CudaContext();

	CudaContext(const CudaContext&) = delete;
	CudaContext& operator=(const CudaContext&) = delete;

	/**
	 * Whether the context initialized its stream and events successfully.
	 */
	bool Valid() const { return valid_; }

	/**
	 * (Re)allocate device framebuffer and pinned staging for the given size.
	 */
	void EnsureSized(int width, int height);

	/**
	 * Launch a clear kernel over the entire device framebuffer.
	 */
	void ClearDevice(std::uint32_t packed_color);

	/**
	 * Execute draw commands on device, preserving submission order.
	 */
	void RenderCommands(const DrawCommand* commands, std::size_t count);

	/**
	 * Generate and rasterize the spiro scene fully on device.
	 *
	 * Returns the number of line segments drawn.
	 */
	int SpiroScene(int width, int height, int instances, int segments, double phase, double dt);

	/**
	 * Clear + generate + rasterize the spiro scene via a captured CUDA graph.
	 *
	 * For a stable scene shape the recurring launch sequence is captured once and
	 * replayed each frame, refreshing only phase/dt through a pinned param buffer.
	 * The device->host present copy stays outside the graph (in ReadbackToHost).
	 *
	 * Returns the number of line segments drawn.
	 */
	int SpiroSceneGraphed(int width, int height, int instances, int segments, double phase, double dt, std::uint32_t clear_packed);

	/**
	 * Clear + generate + rasterize the spiro scene via direct kernel launches.
	 *
	 * Fuses clear and scene into one host-side call (no CUDA graph capture
	 * overhead). Preferred hot path for uncapped benchmarks.
	 */
	int SpiroSceneFrameDirect(int width, int height, int instances, int segments, double phase, double dt, std::uint32_t clear_packed);

	/**
	 * Copy the device framebuffer into host memory via pinned staging.
	 */
	void ReadbackToHost(std::uint8_t* dst, std::size_t bytes);

	/**
	 * Enable/disable one-frame-deep present pipelining.
	 */
	void SetPipelined(bool enabled);

	/**
	 * Pipelined present: issue this frame's readback asynchronously and return a
	 * pointer to the previous frame's completed pixels to blit now (or nullptr on
	 * the first frame). Lets the CPU present overlap the next frame's GPU work.
	 */
	const std::uint8_t* PresentPipelined(std::size_t bytes);

	/**
	 * Timing breakdown for the most recently completed frame.
	 */
	GpuTimings LastTimings() const { return last_timings_; }

private:
	/**
	 * Record the per-frame start event on first device work of the frame.
	 */
	void MarkFrameStart();

	/**
	 * Device buffer receiving this frame's raster output.
	 */
	std::uint32_t* ActiveDeviceFramebuffer() const;

	std::uint32_t* d_framebuffer_[2] = {nullptr, nullptr};
	std::size_t d_framebuffer_capacity_ = 0; // in pixels per buffer

	// The present target (host framebuffer) is page-locked once so device->host
	// readback is a direct pinned DMA with no intermediate staging copy.
	std::uint8_t* registered_host_ptr_ = nullptr;
	std::size_t registered_host_bytes_ = 0;

	// Opaque CUDA handles (cudaStream_t / cudaEvent_t / cudaGraph*) stored as
	// void* so the public header avoids a hard dependency on the CUDA toolkit.
	void* stream_ = nullptr;
	void* copy_stream_ = nullptr;
	void* ev_frame_start_ = nullptr;
	void* ev_after_kernel_ = nullptr;
	void* ev_after_readback_ = nullptr;

	// One-frame-deep present pipeline: double-buffered pinned host staging plus
	// per-buffer timing events so the CPU blit of frame N-1 overlaps the GPU
	// compute + readback of frame N.
	bool pipelined_ = false;
	std::uint8_t* h_present_[2] = {nullptr, nullptr};
	std::size_t present_bytes_ = 0;
	void* ev_p_kstart_[2] = {nullptr, nullptr};
	void* ev_p_kdone_[2] = {nullptr, nullptr};
	void* ev_p_d2h_[2] = {nullptr, nullptr};
	unsigned long long pframe_ = 0;
	bool present_has_prev_ = false;

	// CUDA Graph replay state for stable scene shapes.
	void* graph_exec_ = nullptr;
	double* d_params_ = nullptr; // device: [phase, dt]
	double* h_params_ = nullptr; // pinned host: [phase, dt]
	int cap_width_ = 0;
	int cap_height_ = 0;
	int cap_instances_ = 0;
	int cap_segments_ = 0;
	std::uint32_t cap_clear_packed_ = 0;
	bool has_graph_ = false;

	int width_ = 0;
	int height_ = 0;
	bool valid_ = false;
	bool frame_started_ = false;
	GpuTimings last_timings_{};
};

} // namespace hyperlite
