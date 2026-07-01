#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"
#include "engine/framebuffer.hpp"
#include "engine/input_state.hpp"

#ifdef _WIN32
#include "engine/win32_window.hpp"
#endif

namespace hyperlite {

/**
 * High-performance immediate renderer state exposed to bindings.
 */
class Engine {
public:
	/**
	 * Construct engine state and platform window.
	 */
	Engine(int width, int height, BackendKind backend_kind, std::string title);

	/**
	 * Begin command recording for next frame.
	 */
	void BeginFrame();

	/**
	 * Submit draw command to active command buffer.
	 */
	void PushCommand(const DrawCommand command);

	/**
	 * Render commands to framebuffer via selected backend.
	 */
	void EndFrame();

	/**
	 * Present latest framebuffer to OS window.
	 */
	void Present();

	/**
	 * Whether the active backend can run scenes fully on the GPU.
	 */
	bool SupportsGpuScene() const;

	/**
	 * Enable/disable one-frame-deep present pipelining (trades +1 frame latency
	 * for higher GPU utilization). Only effective on the GPU backend.
	 */
	void SetPipelined(bool enabled);

	/**
	 * Clear the device framebuffer directly (GPU-native fast path).
	 */
	void ClearGpu(std::uint32_t packed_color);

	/**
	 * Generate and rasterize the spiro benchmark scene fully on device.
	 */
	int SpiroSceneGpu(int width, int height, int instances, int segments, double phase, double dt);

	/**
	 * Clear + generate + rasterize the spiro scene via a captured CUDA graph.
	 *
	 * Returns the number of segments drawn, or -1 if no graph path is available.
	 */
	int SpiroSceneFrameGpu(int width, int height, int instances, int segments, double phase, double dt, std::uint32_t clear_packed);

	/**
	 * Clear + generate + rasterize the spiro scene via direct device launches.
	 */
	int SpiroSceneFrameDirectGpu(int width, int height, int instances, int segments, double phase, double dt, std::uint32_t clear_packed);

	/**
	 * One native call: poll events, fused GPU clear+scene, present.
	 *
	 * Eliminates per-frame Python crossing overhead in benchmark hot loops.
	 */
	int TickGpuSpiro(int width, int height, int instances, int segments, double phase, double dt, std::uint32_t clear_packed);

	/**
	 * Timing breakdown for the most recently presented GPU frame.
	 */
	GpuTimings GpuTimingsLast() const;

	/**
	 * Poll OS events and update current input snapshot.
	 */
	void PollEvents();

	/**
	 * Read-only input state for language bindings.
	 */
	const InputState& GetInputState() const;

	/**
	 * Return active backend debug name.
	 */
	std::string_view BackendName() const;

	/**
	 * Return engine liveness from platform state.
	 */
	bool IsRunning() const;

	/**
	 * Mutable framebuffer view for advanced operations.
	 */
	FrameBuffer& MutableFrameBuffer();

private:
	CommandBuffer command_buffer_{};
	FrameBuffer framebuffer_{};
	InputState input_state_{};
	std::unique_ptr<IRenderBackend> backend_{};
	bool pipelined_ = false;
#ifdef _WIN32
	std::unique_ptr<Win32Window> window_{};
#endif
};

} // namespace hyperlite
