#ifdef _WIN32

#include "engine/dxgi_presenter.hpp"

#include <cstdio>
#include <cstring>

#include <d3d11.h>
#include <dxgi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
#include <cuda_d3d11_interop.h>
#include <cuda_runtime.h>
#endif

namespace hyperlite {

namespace {

#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
/**
 * Log and swallow a CUDA error for DXGI interop.
 */
inline bool CudaOk(const cudaError_t status, const char* what) {
	if (status != cudaSuccess) {
		std::fprintf(stderr, "[hyperlite][dxgi] %s failed: %s\n", what, cudaGetErrorString(status));
		return false;
	}
	return true;
}
#endif

} // namespace

DxgiPresenter::DxgiPresenter() = default;

DxgiPresenter::~DxgiPresenter() {
	Shutdown();
}

bool DxgiPresenter::Initialize(void* hwnd, const int width, const int height) {
	Shutdown();
	if (hwnd == nullptr || width <= 0 || height <= 0) {
		return false;
	}

	DXGI_SWAP_CHAIN_DESC desc{};
	desc.BufferCount = 2;
	desc.BufferDesc.Width = static_cast<UINT>(width);
	desc.BufferDesc.Height = static_cast<UINT>(height);
	desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	desc.OutputWindow = static_cast<HWND>(hwnd);
	desc.SampleDesc.Count = 1;
	desc.Windowed = TRUE;
	desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	const D3D_FEATURE_LEVEL feature_levels[] = {D3D_FEATURE_LEVEL_11_0};
	D3D_FEATURE_LEVEL chosen = D3D_FEATURE_LEVEL_11_0;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		feature_levels,
		1,
		D3D11_SDK_VERSION,
		&desc,
		&swapchain_,
		&device_,
		&chosen,
		&context_);
	if (FAILED(hr)) {
		desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			0,
			feature_levels,
			1,
			D3D11_SDK_VERSION,
			&desc,
			&swapchain_,
			&device_,
			&chosen,
			&context_);
	}
	if (FAILED(hr) || device_ == nullptr || swapchain_ == nullptr || context_ == nullptr) {
		Shutdown();
		return false;
	}

	if (FAILED(swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer_))) || backbuffer_ == nullptr) {
		Shutdown();
		return false;
	}

	width_ = width;
	height_ = height;
	valid_ = true;
	return true;
}

bool DxgiPresenter::HasCudaInterop() const {
#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
	return cuda_resource_ != nullptr;
#else
	return false;
#endif
}

bool DxgiPresenter::TryRegisterCudaInterop() {
#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
	if (!valid_ || cuda_resource_ != nullptr) {
		return cuda_resource_ != nullptr;
	}
	return RegisterCudaInterop();
#else
	return false;
#endif
}

void DxgiPresenter::UnregisterCudaInterop() {
#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
	if (cuda_resource_ != nullptr) {
		cudaGraphicsUnregisterResource(cuda_resource_);
		cuda_resource_ = nullptr;
	}
#endif
}

bool DxgiPresenter::RegisterCudaInterop() {
#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
	if (backbuffer_ == nullptr) {
		return false;
	}
	if (!CudaOk(cudaGraphicsD3D11RegisterResource(&cuda_resource_, backbuffer_, cudaGraphicsRegisterFlagsNone), "cudaGraphicsD3D11RegisterResource")) {
		return false;
	}
	return CudaOk(
		cudaGraphicsResourceSetMapFlags(cuda_resource_, cudaGraphicsMapFlagsWriteDiscard),
		"cudaGraphicsResourceSetMapFlags");
#else
	return false;
#endif
}

bool DxgiPresenter::CopyFromHostAndPresent(const std::uint8_t* host_pixels, const int width, const int height) {
	if (!valid_ || host_pixels == nullptr || width != width_ || height != height_ || swapchain_ == nullptr || context_ == nullptr) {
		return false;
	}

	ID3D11Texture2D* frame_buffer = nullptr;
	HRESULT hr = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&frame_buffer));
	if (FAILED(hr) || frame_buffer == nullptr) {
		return false;
	}

	const UINT row_pitch = static_cast<UINT>(width) * 4U;
	context_->UpdateSubresource(frame_buffer, 0, nullptr, host_pixels, row_pitch, 0);
	frame_buffer->Release();

	return SUCCEEDED(swapchain_->Present(vsync_ ? 1 : 0, 0));
}

bool DxgiPresenter::CopyFromDeviceAndPresent(
	const std::uint32_t* device_pixels,
	const int width,
	const int height,
	void* cuda_stream) {
#if defined(HYPERLITE_HAS_CUDA) && HYPERLITE_HAS_CUDA
	if (!valid_ || device_pixels == nullptr || width != width_ || height != height_ || cuda_resource_ == nullptr) {
		return false;
	}
	const auto stream = static_cast<cudaStream_t>(cuda_stream);

	if (!CudaOk(cudaGraphicsMapResources(1, &cuda_resource_, stream), "cudaGraphicsMapResources")) {
		return false;
	}

	cudaArray_t mapped_array = nullptr;
	if (!CudaOk(cudaGraphicsSubResourceGetMappedArray(&mapped_array, cuda_resource_, 0, 0), "cudaGraphicsSubResourceGetMappedArray")) {
		cudaGraphicsUnmapResources(1, &cuda_resource_, stream);
		return false;
	}

	const std::size_t row_bytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
	if (!CudaOk(
		cudaMemcpy2DToArrayAsync(
			mapped_array,
			0,
			0,
			device_pixels,
			row_bytes,
			row_bytes,
			static_cast<std::size_t>(height),
			cudaMemcpyDeviceToDevice,
			stream),
		"cudaMemcpy2DToArrayAsync")) {
		cudaGraphicsUnmapResources(1, &cuda_resource_, stream);
		return false;
	}

	if (!CudaOk(cudaGraphicsUnmapResources(1, &cuda_resource_, stream), "cudaGraphicsUnmapResources")) {
		return false;
	}
	if (!CudaOk(cudaStreamSynchronize(stream), "cudaStreamSynchronize(dxgi)")) {
		return false;
	}

	if (swapchain_ == nullptr || FAILED(swapchain_->Present(vsync_ ? 1 : 0, 0))) {
		return false;
	}
	return true;
#else
	(void)device_pixels;
	(void)width;
	(void)height;
	(void)cuda_stream;
	return false;
#endif
}

void DxgiPresenter::SetVsync(const bool enabled) {
	vsync_ = enabled;
}

void DxgiPresenter::Shutdown() {
	UnregisterCudaInterop();
	if (backbuffer_ != nullptr) {
		backbuffer_->Release();
		backbuffer_ = nullptr;
	}
	if (swapchain_ != nullptr) {
		swapchain_->Release();
		swapchain_ = nullptr;
	}
	if (context_ != nullptr) {
		context_->Release();
		context_ = nullptr;
	}
	if (device_ != nullptr) {
		device_->Release();
		device_ = nullptr;
	}
	valid_ = false;
	width_ = 0;
	height_ = 0;
}

bool DxgiPresenter::Resize(void* hwnd, const int width, const int height) {
	if (hwnd == nullptr || width <= 0 || height <= 0) {
		return false;
	}
	Shutdown();
	return Initialize(hwnd, width, height);
}

} // namespace hyperlite

#endif
