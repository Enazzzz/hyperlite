#include "engine/backend_interface.hpp"

#include "engine/atlas_store.hpp"
#include "engine/rasterizer.hpp"

namespace hyperlite {

namespace {

/**
 * Reference high-performance software backend.
 */
class CpuBackend final : public IRenderBackend {
public:
	void Render(const CommandBuffer& command_buffer, FrameBuffer& framebuffer, const AtlasStore& atlases) override {
		raster::ExecuteCommandBuffer(command_buffer, framebuffer, atlases);
	}

	std::string_view Name() const override {
		return "cpu";
	}
};

} // namespace

std::unique_ptr<IRenderBackend> CreateCpuBackend() {
	return std::make_unique<CpuBackend>();
}

} // namespace hyperlite
