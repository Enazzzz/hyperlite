#include "engine/backend_interface.hpp"

#include <memory>

namespace hyperlite {

std::unique_ptr<IRenderBackend> CreateCpuBackend();
std::unique_ptr<IRenderBackend> CreateCudaBackendOrFallback();

std::unique_ptr<IRenderBackend> CreateBackend(const BackendKind kind) {
	if (kind == BackendKind::kGpu) {
		return CreateCudaBackendOrFallback();
	}
	return CreateCpuBackend();
}

} // namespace hyperlite
