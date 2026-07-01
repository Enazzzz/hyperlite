#include "engine/backend_interface.hpp"

#include "engine/rasterizer.hpp"

#if HYPERLITE_HAS_CUDA
#include "engine/cuda/cuda_context.hpp"
#endif

namespace hyperlite {

std::unique_ptr<IRenderBackend> CreateCpuBackend();

#if HYPERLITE_HAS_CUDA

namespace {

/**
 * GPU-first backend: scene generation and rasterization run on device, the CPU
 * only stages presentation. The device framebuffer is owned by a persistent
 * CudaContext so no allocations happen in the hot loop.
 */
class CudaBackend final : public IRenderBackend {
public:
	void EnsureSized(const int width, const int height) override {
		context_.EnsureSized(width, height);
	}

	void Render(const CommandBuffer& command_buffer, FrameBuffer& framebuffer) override {
		context_.EnsureSized(framebuffer.Width(), framebuffer.Height());
		context_.RenderCommands(command_buffer.Data(), command_buffer.Size());
	}

	void ReadbackToHost(FrameBuffer& framebuffer) override {
		context_.ReadbackToHost(framebuffer.Data(), framebuffer.SizeBytes());
	}

	std::string_view Name() const override {
		return "gpu";
	}

	bool SupportsGpuScene() const override {
		return true;
	}

	void ClearDevice(const std::uint32_t packed_color) override {
		context_.ClearDevice(packed_color);
	}

	int SpiroSceneDevice(
		const int width,
		const int height,
		const int instances,
		const int segments,
		const double phase,
		const double dt) override {
		return context_.SpiroScene(width, height, instances, segments, phase, dt);
	}

	int SpiroSceneFrameGraphed(
		const int width,
		const int height,
		const int instances,
		const int segments,
		const double phase,
		const double dt,
		const std::uint32_t clear_packed) override {
		return context_.SpiroSceneGraphed(width, height, instances, segments, phase, dt, clear_packed);
	}

	int SpiroSceneFrameDirect(
		const int width,
		const int height,
		const int instances,
		const int segments,
		const double phase,
		const double dt,
		const std::uint32_t clear_packed) override {
		return context_.SpiroSceneFrameDirect(width, height, instances, segments, phase, dt, clear_packed);
	}

	GpuTimings LastTimings() const override {
		return context_.LastTimings();
	}

	void SetPipelined(const bool enabled) override {
		context_.SetPipelined(enabled);
	}

	const std::uint8_t* PresentPipelined(const std::size_t bytes) override {
		return context_.PresentPipelined(bytes);
	}

	/**
	 * Whether the device context initialized successfully.
	 */
	bool IsValid() const {
		return context_.Valid();
	}

private:
	CudaContext context_{};
};

} // namespace

std::unique_ptr<IRenderBackend> CreateCudaBackendOrFallback() {
	if (CudaContext::IsAvailable()) {
		auto backend = std::make_unique<CudaBackend>();
		if (backend->IsValid()) {
			return backend;
		}
	}
	// No usable CUDA device/context: keep rendering correct on the CPU.
	return CreateCpuBackend();
}

#else

std::unique_ptr<IRenderBackend> CreateCudaBackendOrFallback() {
	return CreateCpuBackend();
}

#endif

} // namespace hyperlite
