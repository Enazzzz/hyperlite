#pragma once

#ifdef _WIN32

#include <cstdint>

struct HWND__;
struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11Texture2D;
struct cudaGraphicsResource;

namespace hyperlite {

/**
 * DXGI flip-model swapchain presenter for CPU host uploads and optional CUDA interop.
 */
class DxgiPresenter {
public:
	DxgiPresenter();
	~DxgiPresenter();

	DxgiPresenter(const DxgiPresenter&) = delete;
	DxgiPresenter& operator=(const DxgiPresenter&) = delete;

	/**
	 * Create D3D11 device/swapchain bound to a Win32 window.
	 */
	bool Initialize(void* hwnd, int width, int height);

	/**
	 * Whether the DXGI swapchain is ready for presents.
	 */
	bool Valid() const { return valid_; }

	/**
	 * Whether CUDA can copy device pixels into the swapchain backbuffer.
	 */
	bool HasCudaInterop() const;

	/**
	 * Upload host RGBA8 pixels into the swapchain backbuffer and present.
	 */
	bool CopyFromHostAndPresent(const std::uint8_t* host_pixels, int width, int height);

	/**
	 * Copy a device-resident RGBA8 framebuffer into the swapchain backbuffer and present.
	 */
	bool CopyFromDeviceAndPresent(const std::uint32_t* device_pixels, int width, int height, void* cuda_stream);

	/**
	 * Register swapchain backbuffer with CUDA for device-side copies.
	 */
	bool TryRegisterCudaInterop();

	/**
	 * Release CUDA interop while keeping the D3D11 swapchain alive.
	 */
	void UnregisterCudaInterop();

	/**
	 * Release D3D/CUDA interop resources.
	 */
	void Shutdown();

	/**
	 * Recreate swapchain buffers for a new client size.
	 */
	bool Resize(void* hwnd, int width, int height);

	/**
	 * Enable sync-interval present (vsync) on the swapchain.
	 */
	void SetVsync(bool enabled);

private:
	/**
	 * Register the swapchain backbuffer with CUDA for device-side copies.
	 */
	bool RegisterCudaInterop();

	bool valid_ = false;
	bool vsync_ = true;
	int width_ = 0;
	int height_ = 0;
	ID3D11Device* device_ = nullptr;
	ID3D11DeviceContext* context_ = nullptr;
	IDXGISwapChain* swapchain_ = nullptr;
	ID3D11Texture2D* backbuffer_ = nullptr;
	cudaGraphicsResource* cuda_resource_ = nullptr;
};

} // namespace hyperlite

#endif
