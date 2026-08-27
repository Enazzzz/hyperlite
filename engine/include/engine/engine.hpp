#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <chrono>
#include <vector>

#include "engine/atlas_store.hpp"
#include "engine/backend_interface.hpp"
#include "engine/command_buffer.hpp"
#include "engine/depth_buffer.hpp"
#include "engine/framebuffer.hpp"
#include "engine/input_state.hpp"
#include "engine/iwindow.hpp"
#include "engine/mesh_store.hpp"
#include "engine/retained_layer.hpp"
#include "engine/sprite_draw.hpp"

#ifdef _WIN32
#include "engine/dxgi_presenter.hpp"
#endif

namespace hyperlite {

/**
 * Timing breakdown for wireframe fused paths (milliseconds).
 */
struct WireframeTimings {
	float raster_ms = 0.0f;
	float present_ms = 0.0f;
};

/**
 * High-performance immediate renderer state exposed to bindings.
 */
class Engine {
public:
	/**
	 * Construct engine state and platform window (or headless present surface).
	 *
	 * present_mode defaults to kAuto: open a window when a display is available,
	 * otherwise headless. Override with HYPERLITE_HEADLESS / HYPERLITE_PRESENT.
	 */
	Engine(
		int width,
		int height,
		BackendKind backend_kind,
		std::string title,
		PresentMode present_mode = PresentMode::kAuto);

	/**
	 * Flush async presents and release platform resources.
	 */
	~Engine();

	/**
	 * Begin command recording for next frame.
	 */
	void BeginFrame();

	/**
	 * Submit draw command to active command buffer.
	 */
	void PushCommand(const DrawCommand command);

	/**
	 * Submit a contiguous command range to the active command buffer.
	 */
	void PushCommandsRange(const DrawCommand* commands, std::size_t count);

	/**
	 * Render commands to framebuffer via selected backend.
	 */
	void EndFrame();

	/**
	 * Present latest framebuffer to OS window.
	 */
	void Present();

	/**
	 * Upload a full RGBA8 frame as this frame's base image.
	 */
	void UploadFrameRgba(const std::uint8_t* src, std::size_t bytes);

	/**
	 * Blit an RGBA8 sub-image into this frame's command queue.
	 */
	void BlitRgba(const std::uint8_t* src, std::size_t bytes, int dst_x, int dst_y, int width, int height);

	/**
	 * Load a resident RGBA8 atlas and return a handle.
	 */
	int LoadAtlas(const std::uint8_t* src, std::size_t bytes, int width, int height);

	/**
	 * Draw a sub-rectangle from a loaded atlas.
	 */
	void DrawSprite(int atlas_id, int src_x, int src_y, int width, int height, int dst_x, int dst_y);

	/**
	 * Enable partial Win32 GDI presents when the dirty region is smaller than the full frame.
	 *
	 * Ignored when DXGI present is active (default).
	 */
	void SetDirtyPresent(bool enabled);

	/**
	 * Enable DXGI flip-model present (default on). Falls back to GDI when unavailable.
	 */
	void SetDxgiPresent(bool enabled);

	/**
	 * Whether DXGI swapchain present is enabled.
	 */
	bool DxgiPresentEnabled() const;

	/**
	 * Enable GPU-direct CUDA→DXGI present (skips CPU readback on the GPU backend).
	 */
	void SetDirectPresent(bool enabled);

	/**
	 * Set the blit material-sort threshold (0 disables sorting).
	 */
	void SetBlitSortThreshold(std::size_t threshold);

	/**
	 * Capture the current command buffer as a reusable retained layer.
	 */
	int CommitRetainedLayer();

	/**
	 * Replay a retained layer into the active frame command buffer.
	 */
	void DrawRetainedLayer(int layer_handle);

	/**
	 * Poll events, clear, draw sprites, execute, and present in one native call.
	 */
	int TickBlits(std::uint32_t clear_packed, const SpriteDrawDesc* sprites, std::size_t sprite_count);

	/**
	 * Poll events, execute EndFrame, then present in one call.
	 */
	void Tick();

	/**
	 * Poll events, clear, raster many line segments, and present in one native call.
	 *
	 * segments is line_count * 4 int32 values: x0,y0,x1,y1 per segment.
	 */
	int TickLines(
		std::uint32_t clear_packed,
		const std::int32_t* segments,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Poll events, clear, queue line segments, execute, and present in one call.
	 */
	int TickLinesPoll(
		std::uint32_t clear_packed,
		const std::int32_t* segments,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Poll events, clear, raster line segments on the GPU backend, and present.
	 */
	int TickLinesGpu(
		std::uint32_t clear_packed,
		const std::int32_t* segments,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Queue many line commands from packed int32 tuples without per-line Python crossings.
	 */
	void LinesBulk(
		const std::int32_t* segments,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Queue many line commands with per-segment packed colors.
	 */
	void LinesBulkColored(
		const std::int32_t* segments,
		const std::uint32_t* colors,
		std::size_t line_count,
		int line_width = 1);

	/**
	 * Allocate or free the float32 depth plane (matches framebuffer; resizes with window).
	 *
	 * When disabled, 2D behavior matches the pre-3D path exactly.
	 */
	void EnableDepth(bool enabled);

	/**
	 * Whether a depth plane is currently allocated and used by 3D lines.
	 */
	bool DepthEnabled() const;

	/**
	 * Set the column-major 4x4 world→clip matrix (16 float32). Identity = world is clip.
	 */
	void SetViewProj(const float* matrix16);

	/**
	 * Fused poll + clear color/depth + world-space 3D lines + present.
	 *
	 * world_segs: line_count * 6 float32 values — x0,y0,z0, x1,y1,z1 per segment.
	 */
	int TickLines3d(
		std::uint32_t clear_packed,
		const float* world_segs,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Flush pending 2D commands, then raster world-space 3D lines (uses view-proj + depth if on).
	 *
	 * Call inside begin_frame / tick so HUD 2D draws after this still composite on top.
	 */
	void Lines3d(
		const float* world_segs,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Screen-space escape hatch: pixel xy + NDC z in [-1,1] per endpoint (float32 x6).
	 *
	 * Skips view-proj and frustum clip. Depth-tests when depth is enabled.
	 */
	void Lines3dScreen(
		const float* screen_segs,
		std::size_t line_count,
		std::uint32_t line_packed,
		int line_width = 1);

	/**
	 * Enable/disable backface cull for world-space triangles (default on).
	 *
	 * Cull keeps OpenGL-front faces (CCW in NDC → CW / negative area after viewport Y flip).
	 * Screen-space tris ignore this unless you pass cull through TrisScreen's engine default
	 * (TrisScreen uses cull off; world paths use this flag).
	 */
	void SetCullBackfaces(bool enabled);

	/**
	 * Whether world-space triangle backface cull is enabled.
	 */
	bool CullBackfaces() const;

	/**
	 * Fused poll + clear color/depth + world-space filled triangles + present.
	 *
	 * world_verts: tri_count * 9 float32 — x0,y0,z0, x1,y1,z1, x2,y2,z2 per triangle.
	 */
	int TickTris3d(
		std::uint32_t clear_packed,
		const float* world_verts,
		std::size_t tri_count,
		std::uint32_t tri_packed);

	/**
	 * Flush pending 2D, then raster world-space filled triangles (view-proj + depth if on).
	 */
	void Tris3d(
		const float* world_verts,
		std::size_t tri_count,
		std::uint32_t tri_packed);

	/**
	 * Screen-space filled triangles: pixel xy + NDC z in [-1,1] per vertex (float32 x9).
	 *
	 * Skips view-proj and frustum clip. Backface cull is off for this path.
	 */
	void TrisScreen(
		const float* screen_verts,
		std::size_t tri_count,
		std::uint32_t tri_packed);

	/**
	 * Load a retained mesh (CPU-resident) and return a stable handle.
	 *
	 * verts: float32, 6 floats/vert — x,y,z,u,v,_pad. indices: uint32 tris (optional;
	 * nullptr / index_count 0 = triangle list). Resident until Engine dies.
	 */
	int LoadMesh(
		const float* verts,
		std::size_t vert_floats,
		const std::uint32_t* indices,
		std::size_t index_count);

	/**
	 * Draw a loaded mesh with a column-major 4x4 model matrix (mesh-local → world).
	 *
	 * Combines model with the current view-proj, then rasterizes through the Layer 1
	 * tiled path (depth + backface cull). Invalid handle is a no-op.
	 */
	void DrawMesh(int mesh_id, const float* model16, std::uint32_t tri_packed);

	/**
	 * Draw one mesh with many model matrices (column-major 4x4 each, contiguous float32).
	 *
	 * Transform/clip/emit runs per instance; tile bin + fill runs once. Invalid mesh or
	 * instance_count == 0 is a no-op. N=1 delegates to DrawMesh for parity.
	 */
	void DrawMeshMany(
		int mesh_id,
		const float* models16,
		std::size_t instance_count,
		std::uint32_t tri_packed);

	/**
	 * Fused poll + clear color/depth + draw_mesh + present.
	 */
	int TickMesh(
		std::uint32_t clear_packed,
		int mesh_id,
		const float* model16,
		std::uint32_t tri_packed);

	/**
	 * Draw a loaded mesh textured from an atlas (Layer 2.1).
	 *
	 * Samples atlas with perspective-correct UVs (nearest, clamp to [0,1] over the
	 * full atlas). Invalid mesh or atlas handle is a no-op. Honors depth + cull.
	 */
	void DrawMeshTextured(int mesh_id, const float* model16, int atlas_id);

	/**
	 * Draw one mesh with many model matrices textured from an atlas.
	 *
	 * Same layout as DrawMeshMany; invalid mesh/atlas or instance_count == 0 is a no-op.
	 */
	void DrawMeshTexturedMany(
		int mesh_id,
		const float* models16,
		std::size_t instance_count,
		int atlas_id);

	/**
	 * Fused poll + clear color/depth + draw_mesh_textured + present.
	 */
	int TickMeshTextured(
		std::uint32_t clear_packed,
		int mesh_id,
		const float* model16,
		int atlas_id);

	/**
	 * Queue many put-pixel commands from interleaved int32 x,y pairs.
	 */
	void PutPixelsBuffer(const std::int32_t* xy_pairs, std::size_t count, std::uint32_t packed_color);

	/**
	 * Mutable host framebuffer pointer (RGBA8).
	 */
	std::uint8_t* FramebufferPtr();

	/**
	 * Host framebuffer byte size.
	 */
	std::size_t FramebufferBytes() const;

	/**
	 * Read-only depth sample at pixel (1.0 if depth off / OOB). For tests.
	 */
	float DepthAt(int x, int y) const;

	/**
	 * Replay a retained layer directly on the active backend (GPU path).
	 */
	int DrawRetainedLayerGpu(int layer_handle);

	/**
	 * Timing breakdown for the most recently completed wireframe frame.
	 */
	WireframeTimings WireframeTimingsLast() const;

	/**
	 * Whether the active backend can run scenes fully on the GPU.
	 */
	bool SupportsGpuScene() const;

	/**
	 * Enable/disable one-frame-deep double-buffered present (CPU and GPU).
	 *
	 * Renders into a back buffer while displaying the previous frame (+1 frame
	 * latency). On CPU, GDI BitBlt runs on a worker thread so raster can overlap
	 * present. Incompatible with dirty partial presents and GPU direct present.
	 */
	void SetPipelined(bool enabled);

	/**
	 * Alias for SetPipelined — enables double-buffered present.
	 */
	void SetDoubleBufferedPresent(bool enabled) { SetPipelined(enabled); }

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
	 * Seconds elapsed between the two most recent BeginFrame calls.
	 */
	double DeltaTime() const;

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
	 * Capture or release the mouse cursor for FPS-style look/movement.
	 */
	void SetMouseCaptured(bool captured);

	/**
	 * Whether the mouse cursor is currently captured.
	 */
	bool MouseCaptured() const;

	/**
	 * Enter or leave borderless fullscreen on the active monitor.
	 */
	void SetFullscreen(bool fullscreen);

	/**
	 * Whether borderless fullscreen is active.
	 */
	bool IsFullscreen() const;

	/**
	 * Current framebuffer / client size in pixels.
	 */
	void WindowSize(int& width, int& height) const;

	/**
	 * Resize the OS window client area and internal framebuffer.
	 */
	void SetWindowSize(int width, int height);

	/**
	 * Pre-reserve command-buffer capacity (default grows lazily from a small initial size).
	 */
	void SetCommandBufferReserve(std::size_t capacity);

	/**
	 * Stable-sort long contiguous line runs by color/width before rasterization (0 disables).
	 */
	void SetLineSortThreshold(std::size_t threshold);

	/**
	 * Enable vertical sync for CPU GDI and GPU DXGI present paths.
	 */
	void SetVsync(bool enabled);

	/**
	 * Whether vsync is enabled.
	 */
	bool VsyncEnabled() const;

	/**
	 * Mutable framebuffer view for advanced operations.
	 */
	FrameBuffer& MutableFrameBuffer();

private:
	/**
	 * Resize internal render targets after the window client area changes.
	 */
	void ApplyFramebufferResize(int width, int height);

	/**
	 * Writable render target for the current frame (ping-pongs when pipelined on CPU).
	 */
	FrameBuffer& ActiveFramebuffer();

	/**
	 * Read-only view of the active render target.
	 */
	const FrameBuffer& ActiveFramebuffer() const;

	/**
	 * Return pixels from the previous CPU frame for pipelined present, or nullptr.
	 */
	const std::uint8_t* PresentPipelinedCpu();

	/**
	 * Whether the active backend is the CPU reference path.
	 */
	bool IsCpuBackend() const;

	/**
	 * Flush queued 2D commands so subsequent immediate 3D draws composite correctly.
	 */
	void FlushPending2d();

	/**
	 * Active depth plane pointer when enabled, else nullptr.
	 */
	DepthBuffer* ActiveDepth();

	/**
	 * Create or validate the DXGI swapchain presenter.
	 */
	void EnsureDxgiPresenter();

	/**
	 * Present host RGBA8 pixels through DXGI when available, otherwise GDI.
	 */
	bool PresentHostFrame(const std::uint8_t* pixels, int width, int height);

	/**
	 * Present one framebuffer through DXGI or GDI.
	 */
	void PresentFramebuffer(const FrameBuffer& framebuffer);

	CommandBuffer command_buffer_{};
	FrameBuffer framebuffer_{};
	FrameBuffer framebuffer_alt_{};
	DepthBuffer depth_buffer_{};
	bool depth_enabled_ = false;
	bool cull_backfaces_ = true;
	std::array<float, 16> view_proj_{
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f};
	AtlasStore atlas_store_{};
	MeshStore mesh_store_{};
	InputState input_state_{};
	std::unique_ptr<IRenderBackend> backend_{};
	bool pipelined_ = false;
	bool cpu_present_has_prev_ = false;
	std::uint32_t cpu_frame_ = 0;
	bool dirty_present_ = true;
	bool dxgi_present_ = true;
	bool direct_present_ = false;
	bool vsync_ = true;
	std::size_t blit_sort_threshold_ = 256;
	std::size_t line_sort_threshold_ = 64;
	std::size_t command_buffer_reserve_ = 1U << 16U;
	std::vector<RetainedLayer> retained_layers_{};
	std::chrono::steady_clock::time_point last_frame_tick_{};
	bool has_last_frame_tick_ = false;
	double delta_time_seconds_ = 0.0;
	float record_ms_ = 0.0f;
	float present_ms_ = 0.0f;
	float wireframe_raster_ms_ = 0.0f;
	std::chrono::steady_clock::time_point record_start_{};
	bool record_active_ = false;
	std::unique_ptr<IWindow> window_{};
#ifdef _WIN32
	std::unique_ptr<DxgiPresenter> dxgi_presenter_{};
#endif
};

} // namespace hyperlite
