#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"

namespace hyperlite {

/**
 * Per-frame GPU stage timing breakdown (milliseconds).
 *
 * Populated by GPU backends so Python telemetry can attribute frame cost to
 * host->device upload, kernel execution, and device->host readback.
 */
struct GpuTimings {
	float upload_ms = 0.0f;
	float kernel_ms = 0.0f;
	float readback_ms = 0.0f;
};

/**
 * Runtime-selectable rendering backend.
 */
class IRenderBackend {
public:
	virtual ~IRenderBackend() = default;

	/**
	 * Ensure backend-owned buffers match the requested framebuffer size.
	 *
	 * No-op for the CPU backend; GPU backends (re)allocate device storage.
	 */
	virtual void EnsureSized(int /*width*/, int /*height*/) {}

	/**
	 * Execute draw commands into target framebuffer.
	 */
	virtual void Render(const CommandBuffer& command_buffer, FrameBuffer& framebuffer) = 0;

	/**
	 * Make the host-side framebuffer reflect the latest render.
	 *
	 * CPU backend renders directly into host memory so this is a no-op. GPU
	 * backends perform a device->host copy here, deferred until present time.
	 */
	virtual void ReadbackToHost(FrameBuffer& /*framebuffer*/) {}

	/**
	 * Backend debug name for diagnostics.
	 */
	virtual std::string_view Name() const = 0;

	/**
	 * Whether this backend can generate/rasterize scenes fully on device.
	 */
	virtual bool SupportsGpuScene() const { return false; }

	/**
	 * Clear the device framebuffer directly (GPU-native path).
	 */
	virtual void ClearDevice(std::uint32_t /*packed_color*/) {}

	/**
	 * Generate and rasterize the spiro benchmark scene fully on device.
	 *
	 * Returns the number of line segments drawn. Default returns 0 to signal
	 * the GPU-native path is unavailable so callers fall back to commands.
	 */
	virtual int SpiroSceneDevice(
		int /*width*/,
		int /*height*/,
		int /*instances*/,
		int /*segments*/,
		double /*phase*/,
		double /*dt*/) {
		return 0;
	}

	/**
	 * Clear + generate + rasterize the spiro scene through a captured CUDA graph.
	 *
	 * Returns the number of segments drawn, or -1 when the backend has no graph
	 * path so the caller can use the direct device path instead.
	 */
	virtual int SpiroSceneFrameGraphed(
		int /*width*/,
		int /*height*/,
		int /*instances*/,
		int /*segments*/,
		double /*phase*/,
		double /*dt*/,
		std::uint32_t /*clear_packed*/) {
		return -1;
	}

	/**
	 * Clear + generate + rasterize the spiro scene via direct kernel launches.
	 *
	 * Returns the number of segments drawn, or -1 when unavailable.
	 */
	virtual int SpiroSceneFrameDirect(
		int /*width*/,
		int /*height*/,
		int /*instances*/,
		int /*segments*/,
		double /*phase*/,
		double /*dt*/,
		std::uint32_t /*clear_packed*/) {
		return -1;
	}

	/**
	 * Timing breakdown of the most recently completed frame.
	 */
	virtual GpuTimings LastTimings() const { return {}; }

	/**
	 * Enable/disable one-frame-deep present pipelining (GPU backends only).
	 */
	virtual void SetPipelined(bool /*enabled*/) {}

	/**
	 * Pipelined present: returns the previous frame's completed pixels to blit
	 * now (or nullptr when unsupported / on the first frame).
	 */
	virtual const std::uint8_t* PresentPipelined(std::size_t /*bytes*/) { return nullptr; }
};

/**
 * Backends available for runtime selection.
 */
enum class BackendKind {
	kCpu,
	kGpu
};

/**
 * Build backend implementation for the requested kind.
 */
std::unique_ptr<IRenderBackend> CreateBackend(BackendKind kind);

} // namespace hyperlite
