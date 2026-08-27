#include "engine/engine.hpp"

#include "engine/cpu_line_raster_3d.hpp"
#include "engine/cpu_tri_raster_3d.hpp"
#include "engine/rasterizer.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace hyperlite {

Engine::Engine(
	const int width,
	const int height,
	const BackendKind backend_kind,
	std::string title,
	const PresentMode present_mode)
	: framebuffer_(width, height),
	  backend_(CreateBackend(backend_kind)) {
	command_buffer_.Reserve(command_buffer_reserve_);
	if (!backend_) {
		throw std::runtime_error("Failed to create rendering backend.");
	}
	backend_->EnsureSized(width, height);
	window_ = CreatePlatformWindow(width, height, std::move(title), present_mode);
	if (window_) {
		window_->SetVsync(vsync_);
		const int client_w = window_->Width();
		const int client_h = window_->Height();
		if (client_w != width || client_h != height) {
			framebuffer_.Resize(client_w, client_h);
			backend_->EnsureSized(client_w, client_h);
		}
	}
#ifdef _WIN32
	EnsureDxgiPresenter();
#endif
}

Engine::~Engine() {
	if (window_) {
		window_->SetAsyncPresent(false);
		window_->FlushAsyncPresent();
	}
}

void Engine::BeginFrame() {
	const auto now = std::chrono::steady_clock::now();
	if (has_last_frame_tick_) {
		delta_time_seconds_ = std::chrono::duration<double>(now - last_frame_tick_).count();
	}
	last_frame_tick_ = now;
	has_last_frame_tick_ = true;
	record_start_ = now;
	record_active_ = true;
	command_buffer_.Reset();
}

void Engine::PushCommand(const DrawCommand command) {
	if (command_buffer_.Size() + 1U > command_buffer_reserve_) {
		command_buffer_reserve_ = std::max(command_buffer_reserve_ * 2U, command_buffer_.Size() + 1U);
		command_buffer_.Reserve(command_buffer_reserve_);
	}
	command_buffer_.Push(command);
}

void Engine::PushCommandsRange(const DrawCommand* commands, const std::size_t count) {
	if (commands == nullptr || count == 0) {
		return;
	}
	if (command_buffer_.Size() + count > command_buffer_reserve_) {
		command_buffer_reserve_ = std::max(command_buffer_reserve_ * 2U, command_buffer_.Size() + count);
		command_buffer_.Reserve(command_buffer_reserve_);
	}
	command_buffer_.PushRange(commands, count);
}

void Engine::EndFrame() {
	if (record_active_) {
		record_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record_start_).count());
		record_active_ = false;
	}
	if (blit_sort_threshold_ > 0) {
		command_buffer_.SortBlitRunsByMaterial(blit_sort_threshold_);
	}
	if (line_sort_threshold_ > 0 && !command_buffer_.LinesAlreadySorted()) {
		command_buffer_.SortLineRunsByColor(line_sort_threshold_);
	}
	if (depth_enabled_ && IsCpuBackend()) {
		raster::ExecuteCommandBuffer(command_buffer_, ActiveFramebuffer(), atlas_store_, ActiveDepth());
	} else {
		backend_->Render(command_buffer_, ActiveFramebuffer(), atlas_store_);
	}
}

void Engine::UploadFrameRgba(const std::uint8_t* src, const std::size_t bytes) {
	command_buffer_.StageUploadFrame(src, bytes);
	command_buffer_.Push(MakeDrawCommand(CommandType::kUploadFrame, 0, 0, 0, 0, 0U));
}

void Engine::BlitRgba(
	const std::uint8_t* src,
	const std::size_t bytes,
	const int dst_x,
	const int dst_y,
	const int width,
	const int height) {
	if (src == nullptr || width <= 0 || height <= 0) {
		return;
	}
	const std::size_t required = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
	if (bytes < required) {
		return;
	}
	const std::uint32_t record_index = command_buffer_.PushInlineBlit(src, bytes, dst_x, dst_y, width, height);
	command_buffer_.Push(MakeDrawCommand(CommandType::kBlit, dst_x, dst_y, width, height, record_index));
}

int Engine::LoadAtlas(const std::uint8_t* src, const std::size_t bytes, const int width, const int height) {
	const int handle = atlas_store_.Load(src, bytes, width, height);
	if (handle < 0) {
		return -1;
	}
	const AtlasEntry* entry = atlas_store_.Get(handle);
	if (entry != nullptr) {
		backend_->EnsureAtlasResident(handle, entry->pixels.data(), entry->pixels.size(), entry->width, entry->height);
	}
	return handle;
}

void Engine::DrawSprite(
	const int atlas_id,
	const int src_x,
	const int src_y,
	const int width,
	const int height,
	const int dst_x,
	const int dst_y) {
	if (width <= 0 || height <= 0 || atlas_id < 0) {
		return;
	}
	const std::uint32_t record_index =
		command_buffer_.PushAtlasBlit(static_cast<std::uint32_t>(atlas_id), src_x, src_y, width, height, dst_x, dst_y);
	command_buffer_.Push(MakeDrawCommand(CommandType::kDrawSprite, dst_x, dst_y, width, height, record_index));
}

void Engine::SetDirtyPresent(const bool enabled) {
	dirty_present_ = enabled;
}

void Engine::SetDxgiPresent(const bool enabled) {
#ifdef _WIN32
	if (dxgi_present_ == enabled) {
		return;
	}
	dxgi_present_ = enabled;
	if (!enabled) {
		if (dxgi_presenter_) {
			dxgi_presenter_->UnregisterCudaInterop();
		}
		backend_->BindDxgiPresenter(nullptr);
		if (direct_present_) {
			direct_present_ = false;
		}
		dxgi_presenter_.reset();
		return;
	}
	EnsureDxgiPresenter();
	if (direct_present_ && backend_->Name() == "gpu" && dxgi_presenter_ != nullptr && dxgi_presenter_->TryRegisterCudaInterop()) {
		backend_->BindDxgiPresenter(dxgi_presenter_.get());
	}
#else
	(void)enabled;
#endif
}

bool Engine::DxgiPresentEnabled() const {
	return dxgi_present_;
}

void Engine::SetDirectPresent(const bool enabled) {
#ifdef _WIN32
	if (enabled && pipelined_) {
		SetPipelined(false);
	}
	direct_present_ = enabled;
	if (!enabled) {
		if (dxgi_presenter_) {
			dxgi_presenter_->UnregisterCudaInterop();
		}
		backend_->BindDxgiPresenter(nullptr);
		return;
	}
	if (window_ == nullptr || backend_->Name() != "gpu") {
		direct_present_ = false;
		return;
	}
	EnsureDxgiPresenter();
	if (dxgi_presenter_ == nullptr || !dxgi_presenter_->Valid()) {
		direct_present_ = false;
		backend_->BindDxgiPresenter(nullptr);
		return;
	}
	if (dxgi_presenter_->TryRegisterCudaInterop()) {
		dxgi_presenter_->SetVsync(vsync_);
		backend_->BindDxgiPresenter(dxgi_presenter_.get());
	} else {
		direct_present_ = false;
		backend_->BindDxgiPresenter(nullptr);
	}
#else
	(void)enabled;
#endif
}

void Engine::SetBlitSortThreshold(const std::size_t threshold) {
	blit_sort_threshold_ = threshold;
}

int Engine::CommitRetainedLayer() {
	const int handle = static_cast<int>(retained_layers_.size());
	retained_layers_.push_back(RetainedLayer::Capture(command_buffer_));
	command_buffer_.Reset();
	return handle;
}

void Engine::DrawRetainedLayer(const int layer_handle) {
	if (layer_handle < 0 || static_cast<std::size_t>(layer_handle) >= retained_layers_.size()) {
		return;
	}
	retained_layers_[static_cast<std::size_t>(layer_handle)].ReplayInto(command_buffer_);
}

int Engine::TickBlits(const std::uint32_t clear_packed, const SpriteDrawDesc* sprites, const std::size_t sprite_count) {
	PollEvents();
	BeginFrame();
	command_buffer_.Push(MakeDrawCommand(CommandType::kClear, 0, 0, 0, 0, clear_packed));
	if (sprites != nullptr) {
		for (std::size_t i = 0; i < sprite_count; ++i) {
			const SpriteDrawDesc& sprite = sprites[i];
			if (sprite.width <= 0 || sprite.height <= 0) {
				continue;
			}
			const std::uint32_t record_index = command_buffer_.PushAtlasBlit(
				sprite.atlas_id,
				sprite.src_x,
				sprite.src_y,
				sprite.width,
				sprite.height,
				sprite.dst_x,
				sprite.dst_y);
			command_buffer_.Push(
				MakeDrawCommand(CommandType::kDrawSprite, sprite.dst_x, sprite.dst_y, sprite.width, sprite.height, record_index));
		}
	}
	EndFrame();
	Present();
	return static_cast<int>(sprite_count);
}

void Engine::Present() {
	const auto present_start = std::chrono::steady_clock::now();
	if (direct_present_ && backend_->SupportsDirectPresent()) {
		(void)backend_->PresentDirect();
		present_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - present_start).count());
		return;
	}
	if (pipelined_) {
		const FrameBuffer& dimensions = ActiveFramebuffer();
		const std::uint8_t* ready = nullptr;
		if (IsCpuBackend()) {
			ready = PresentPipelinedCpu();
		} else {
			ready = backend_->PresentPipelined(dimensions.SizeBytes());
		}
		if (ready != nullptr) {
#ifdef _WIN32
			EnsureDxgiPresenter();
			const bool dxgi_ready = dxgi_present_ && dxgi_presenter_ != nullptr && dxgi_presenter_->Valid();
			if (dxgi_ready) {
				(void)PresentHostFrame(ready, dimensions.Width(), dimensions.Height());
			} else
#endif
			if (window_) {
				if (IsCpuBackend() && window_->AsyncPresentEnabled()) {
					window_->PresentRawAsync(ready, dimensions.Width(), dimensions.Height());
				} else {
					window_->PresentRaw(ready, dimensions.Width(), dimensions.Height());
				}
			}
		}
		present_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - present_start).count());
		return;
	}
	backend_->ReadbackToHost(ActiveFramebuffer());
	const FrameBuffer& framebuffer = ActiveFramebuffer();
#ifdef _WIN32
	EnsureDxgiPresenter();
	if (dxgi_present_ && dxgi_presenter_ != nullptr && dxgi_presenter_->Valid()) {
		PresentFramebuffer(framebuffer);
	} else
#endif
	if (window_) {
		if (dirty_present_ && framebuffer.DirtyActive()) {
			int x0 = 0;
			int y0 = 0;
			int x1 = 0;
			int y1 = 0;
			framebuffer.DirtyBounds(x0, y0, x1, y1);
			const int dirty_w = x1 - x0;
			const int dirty_h = y1 - y0;
			const int full_area = framebuffer.Width() * framebuffer.Height();
			const int dirty_area = dirty_w * dirty_h;
			const auto& dirty_tiles = framebuffer.DirtyTiles();
			if (!dirty_tiles.empty() && dirty_area > 0 && dirty_area * 4 < full_area * 3) {
				for (const std::uint32_t tile : dirty_tiles) {
					int tx0 = 0;
					int ty0 = 0;
					int tx1 = 0;
					int ty1 = 0;
					framebuffer.TileBounds(tile, tx0, ty0, tx1, ty1);
					window_->PresentRect(framebuffer, tx0, ty0, tx1 - tx0, ty1 - ty0);
				}
			} else if (dirty_w > 0 && dirty_h > 0 && dirty_area < full_area) {
				window_->PresentRect(framebuffer, x0, y0, dirty_w, dirty_h);
			} else {
				window_->Present(framebuffer);
			}
		} else {
			window_->Present(framebuffer);
		}
	}
	present_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - present_start).count());
}

void Engine::Tick() {
	PollEvents();
	EndFrame();
	Present();
}

int Engine::TickLines(
	const std::uint32_t clear_packed,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	const auto raster_start = std::chrono::steady_clock::now();
	raster::RasterWireframeSegments(ActiveFramebuffer(), clear_packed, segments, line_count, line_packed, line_width);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	Present();
	return static_cast<int>(line_count);
}

int Engine::TickLinesPoll(
	const std::uint32_t clear_packed,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	PollEvents();
	return TickLines(clear_packed, segments, line_count, line_packed, line_width);
}

int Engine::TickLinesGpu(
	const std::uint32_t clear_packed,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	PollEvents();
	const auto raster_start = std::chrono::steady_clock::now();
	const int device_lines = backend_->TickLinesDevice(
		clear_packed,
		segments,
		line_count,
		line_packed,
		line_width);
	if (device_lines >= 0) {
		wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
		Present();
		return device_lines;
	}
	BeginFrame();
	command_buffer_.Push(MakeDrawCommand(CommandType::kClear, 0, 0, 0, 0, clear_packed));
	LinesBulk(segments, line_count, line_packed, line_width);
	if (line_sort_threshold_ > 0 && !command_buffer_.LinesAlreadySorted()) {
		command_buffer_.SortLineRunsByColor(line_sort_threshold_);
	}
	backend_->Render(command_buffer_, ActiveFramebuffer(), atlas_store_);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	if (record_active_) {
		record_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - record_start_).count());
		record_active_ = false;
	}
	Present();
	return static_cast<int>(line_count);
}

void Engine::LinesBulk(
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	const int clamped = std::max(1, std::min(line_width, 255));
	const std::size_t needed = command_buffer_.Size() + line_count;
	if (needed > command_buffer_reserve_) {
		command_buffer_reserve_ = std::max(command_buffer_reserve_ * 2U, needed);
		command_buffer_.Reserve(command_buffer_reserve_);
	}
	command_buffer_.PushLinesBulk(
		segments,
		line_count,
		line_packed,
		static_cast<std::uint8_t>(clamped));
}

void Engine::LinesBulkColored(
	const std::int32_t* segments,
	const std::uint32_t* colors,
	const std::size_t line_count,
	const int line_width) {
	if (segments == nullptr || colors == nullptr || line_count == 0U) {
		return;
	}
	const int clamped = std::max(1, std::min(line_width, 255));
	const std::size_t needed = command_buffer_.Size() + line_count;
	if (needed > command_buffer_reserve_) {
		command_buffer_reserve_ = std::max(command_buffer_reserve_ * 2U, needed);
		command_buffer_.Reserve(command_buffer_reserve_);
	}
	command_buffer_.PushLinesBulkColored(
		segments,
		colors,
		line_count,
		static_cast<std::uint8_t>(clamped));
}

void Engine::EnableDepth(const bool enabled) {
	if (enabled == depth_enabled_) {
		if (enabled) {
			const FrameBuffer& fb = ActiveFramebuffer();
			if (!depth_buffer_.Allocated() ||
				depth_buffer_.Width() != fb.Width() ||
				depth_buffer_.Height() != fb.Height()) {
				depth_buffer_.Resize(fb.Width(), fb.Height());
			}
		}
		return;
	}
	depth_enabled_ = enabled;
	if (enabled) {
		const FrameBuffer& fb = ActiveFramebuffer();
		depth_buffer_.Resize(fb.Width(), fb.Height());
	} else {
		depth_buffer_.Reset();
	}
}

bool Engine::DepthEnabled() const {
	return depth_enabled_;
}

void Engine::SetViewProj(const float* matrix16) {
	if (matrix16 == nullptr) {
		return;
	}
	std::memcpy(view_proj_.data(), matrix16, sizeof(float) * 16U);
}

DepthBuffer* Engine::ActiveDepth() {
	return (depth_enabled_ && depth_buffer_.Allocated()) ? &depth_buffer_ : nullptr;
}

void Engine::FlushPending2d() {
	if (command_buffer_.Size() == 0U) {
		return;
	}
	if (blit_sort_threshold_ > 0) {
		command_buffer_.SortBlitRunsByMaterial(blit_sort_threshold_);
	}
	if (line_sort_threshold_ > 0 && !command_buffer_.LinesAlreadySorted()) {
		command_buffer_.SortLineRunsByColor(line_sort_threshold_);
	}
	if (depth_enabled_ && IsCpuBackend()) {
		raster::ExecuteCommandBuffer(command_buffer_, ActiveFramebuffer(), atlas_store_, ActiveDepth());
	} else {
		backend_->Render(command_buffer_, ActiveFramebuffer(), atlas_store_);
	}
	command_buffer_.Reset();
}

int Engine::TickLines3d(
	const std::uint32_t clear_packed,
	const float* world_segs,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	PollEvents();
	const auto raster_start = std::chrono::steady_clock::now();
	raster::ClearAndRasterLines3dWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		view_proj_.data(),
		clear_packed,
		world_segs,
		line_count,
		line_packed,
		line_width);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	Present();
	return static_cast<int>(line_count);
}

void Engine::Lines3d(
	const float* world_segs,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	FlushPending2d();
	raster::RasterLines3dWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		view_proj_.data(),
		world_segs,
		line_count,
		line_packed,
		line_width);
}

void Engine::Lines3dScreen(
	const float* screen_segs,
	const std::size_t line_count,
	const std::uint32_t line_packed,
	const int line_width) {
	FlushPending2d();
	raster::RasterLines3dScreen(
		ActiveFramebuffer(),
		ActiveDepth(),
		screen_segs,
		line_count,
		line_packed,
		line_width);
}

void Engine::SetCullBackfaces(const bool enabled) {
	cull_backfaces_ = enabled;
}

bool Engine::CullBackfaces() const {
	return cull_backfaces_;
}

int Engine::TickTris3d(
	const std::uint32_t clear_packed,
	const float* world_verts,
	const std::size_t tri_count,
	const std::uint32_t tri_packed) {
	PollEvents();
	const auto raster_start = std::chrono::steady_clock::now();
	raster::ClearAndRasterTris3dWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		view_proj_.data(),
		clear_packed,
		world_verts,
		tri_count,
		tri_packed,
		cull_backfaces_);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	Present();
	return static_cast<int>(tri_count);
}

void Engine::Tris3d(
	const float* world_verts,
	const std::size_t tri_count,
	const std::uint32_t tri_packed) {
	FlushPending2d();
	raster::RasterTris3dWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		view_proj_.data(),
		world_verts,
		tri_count,
		tri_packed,
		cull_backfaces_);
}

void Engine::TrisScreen(
	const float* screen_verts,
	const std::size_t tri_count,
	const std::uint32_t tri_packed) {
	FlushPending2d();
	// Screen path: backface cull off (caller may wind either way in pixel space).
	raster::RasterTris3dScreen(
		ActiveFramebuffer(),
		ActiveDepth(),
		screen_verts,
		tri_count,
		tri_packed,
		false);
}

int Engine::LoadMesh(
	const float* verts,
	const std::size_t vert_floats,
	const std::uint32_t* indices,
	const std::size_t index_count) {
	return mesh_store_.Load(verts, vert_floats, indices, index_count);
}

void Engine::DrawMesh(const int mesh_id, const float* model16, const std::uint32_t tri_packed) {
	const MeshEntry* mesh = mesh_store_.Get(mesh_id);
	if (mesh == nullptr || model16 == nullptr) {
		return;
	}
	FlushPending2d();
	float mvp[16];
	raster::MulMat4ColumnMajor(view_proj_.data(), model16, mvp);
	const std::uint32_t* indices = mesh->indices.empty() ? nullptr : mesh->indices.data();
	const std::size_t index_count = mesh->indices.size();
	raster::RasterMeshWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		mvp,
		mesh->positions.data(),
		mesh->vertex_count,
		indices,
		index_count,
		tri_packed,
		cull_backfaces_);
}

int Engine::TickMesh(
	const std::uint32_t clear_packed,
	const int mesh_id,
	const float* model16,
	const std::uint32_t tri_packed) {
	const MeshEntry* mesh = mesh_store_.Get(mesh_id);
	if (mesh == nullptr || model16 == nullptr) {
		PollEvents();
		Present();
		return 0;
	}
	PollEvents();
	const auto raster_start = std::chrono::steady_clock::now();
	float mvp[16];
	raster::MulMat4ColumnMajor(view_proj_.data(), model16, mvp);
	const std::uint32_t* indices = mesh->indices.empty() ? nullptr : mesh->indices.data();
	const std::size_t index_count = mesh->indices.size();
	raster::ClearAndRasterMeshWorld(
		ActiveFramebuffer(),
		ActiveDepth(),
		mvp,
		clear_packed,
		mesh->positions.data(),
		mesh->vertex_count,
		indices,
		index_count,
		tri_packed,
		cull_backfaces_);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	Present();
	return static_cast<int>(mesh->triangle_count);
}

float Engine::DepthAt(const int x, const int y) const {
	if (!depth_enabled_ || !depth_buffer_.Allocated()) {
		return 1.0f;
	}
	return depth_buffer_.At(x, y);
}

void Engine::PutPixelsBuffer(const std::int32_t* xy_pairs, const std::size_t count, const std::uint32_t packed_color) {
	if (xy_pairs == nullptr || count == 0U) {
		return;
	}
	const std::size_t needed = command_buffer_.Size() + count;
	if (needed > command_buffer_reserve_) {
		command_buffer_reserve_ = std::max(command_buffer_reserve_ * 2U, needed);
		command_buffer_.Reserve(command_buffer_reserve_);
	}
	std::vector<DrawCommand> batch(count);
	for (std::size_t i = 0U; i < count; ++i) {
		batch[i] = MakeDrawCommand(CommandType::kPutPixel, xy_pairs[i * 2U], xy_pairs[i * 2U + 1U], 0, 0, packed_color);
	}
	command_buffer_.PushRange(batch.data(), batch.size());
}

std::uint8_t* Engine::FramebufferPtr() {
	return ActiveFramebuffer().Data();
}

std::size_t Engine::FramebufferBytes() const {
	return ActiveFramebuffer().SizeBytes();
}

int Engine::DrawRetainedLayerGpu(const int layer_handle) {
	if (layer_handle < 0 || static_cast<std::size_t>(layer_handle) >= retained_layers_.size()) {
		return -1;
	}
	CommandBuffer layer_buffer{};
	retained_layers_[static_cast<std::size_t>(layer_handle)].ReplayInto(layer_buffer);
	const auto raster_start = std::chrono::steady_clock::now();
	backend_->Render(layer_buffer, ActiveFramebuffer(), atlas_store_);
	wireframe_raster_ms_ = static_cast<float>(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - raster_start).count());
	return static_cast<int>(layer_buffer.Size());
}

WireframeTimings Engine::WireframeTimingsLast() const {
	WireframeTimings timings{};
	timings.raster_ms = wireframe_raster_ms_;
	timings.present_ms = present_ms_;
	return timings;
}

bool Engine::SupportsGpuScene() const {
	return backend_->SupportsGpuScene();
}

void Engine::SetPipelined(const bool enabled) {
	if (pipelined_ == enabled) {
		return;
	}
	if (enabled && direct_present_) {
		SetDirectPresent(false);
	}
	pipelined_ = enabled;
	backend_->SetPipelined(enabled);
	cpu_frame_ = 0;
	cpu_present_has_prev_ = false;
	if (enabled && IsCpuBackend()) {
		framebuffer_alt_.Resize(framebuffer_.Width(), framebuffer_.Height());
		if (window_) {
#ifdef _WIN32
			EnsureDxgiPresenter();
			const bool dxgi_ready = dxgi_present_ && dxgi_presenter_ != nullptr && dxgi_presenter_->Valid();
			window_->SetAsyncPresent(!dxgi_ready);
#else
			window_->SetAsyncPresent(false);
#endif
		}
	} else if (!enabled) {
		if (window_) {
			window_->SetAsyncPresent(false);
			window_->FlushAsyncPresent();
		}
	}
}

void Engine::ClearGpu(const std::uint32_t packed_color) {
	backend_->ClearDevice(packed_color);
}

int Engine::SpiroSceneGpu(const int width, const int height, const int instances, const int segments, const double phase, const double dt) {
	return backend_->SpiroSceneDevice(width, height, instances, segments, phase, dt);
}

int Engine::SpiroSceneFrameGpu(const int width, const int height, const int instances, const int segments, const double phase, const double dt, const std::uint32_t clear_packed) {
	return backend_->SpiroSceneFrameGraphed(width, height, instances, segments, phase, dt, clear_packed);
}

int Engine::SpiroSceneFrameDirectGpu(
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	const std::uint32_t clear_packed) {
	return backend_->SpiroSceneFrameDirect(width, height, instances, segments, phase, dt, clear_packed);
}

int Engine::TickGpuSpiro(
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	const std::uint32_t clear_packed) {
	PollEvents();
	const int draws = backend_->SpiroSceneFrameDirect(width, height, instances, segments, phase, dt, clear_packed);
	Present();
	return draws;
}

GpuTimings Engine::GpuTimingsLast() const {
	GpuTimings timings = backend_->LastTimings();
	timings.record_ms = record_ms_;
	timings.present_ms = present_ms_;
	return timings;
}

double Engine::DeltaTime() const {
	return delta_time_seconds_;
}

void Engine::PollEvents() {
	if (window_) {
		window_->PollEvents(input_state_);
		int resize_w = 0;
		int resize_h = 0;
		if (window_->ConsumeResize(resize_w, resize_h)) {
			ApplyFramebufferResize(resize_w, resize_h);
		}
	}
}

const InputState& Engine::GetInputState() const {
	return input_state_;
}

std::string_view Engine::BackendName() const {
	return backend_->Name();
}

bool Engine::IsRunning() const {
	return window_ && window_->IsAlive() && !input_state_.quit_requested;
}

FrameBuffer& Engine::MutableFrameBuffer() {
	return ActiveFramebuffer();
}

bool Engine::IsCpuBackend() const {
	return backend_ != nullptr && backend_->Name() == "cpu";
}

FrameBuffer& Engine::ActiveFramebuffer() {
	if (pipelined_ && IsCpuBackend()) {
		return (cpu_frame_ & 1U) == 0U ? framebuffer_ : framebuffer_alt_;
	}
	return framebuffer_;
}

const FrameBuffer& Engine::ActiveFramebuffer() const {
	if (pipelined_ && IsCpuBackend()) {
		return (cpu_frame_ & 1U) == 0U ? framebuffer_ : framebuffer_alt_;
	}
	return framebuffer_;
}

const std::uint8_t* Engine::PresentPipelinedCpu() {
	const std::uint8_t* ready = nullptr;
	if (cpu_present_has_prev_) {
		const int prev = static_cast<int>((cpu_frame_ - 1U) & 1U);
		ready = prev == 0 ? framebuffer_.Data() : framebuffer_alt_.Data();
	}
	cpu_present_has_prev_ = true;
	++cpu_frame_;
	return ready;
}

void Engine::SetMouseCaptured(const bool captured) {
	if (window_) {
		window_->SetMouseCaptured(captured);
		input_state_.mouse_captured = captured;
	}
}

bool Engine::MouseCaptured() const {
	return input_state_.mouse_captured;
}

void Engine::SetFullscreen(const bool fullscreen) {
	if (window_) {
		window_->SetFullscreen(fullscreen);
	}
}

bool Engine::IsFullscreen() const {
	return window_ && window_->IsFullscreen();
}

void Engine::WindowSize(int& width, int& height) const {
	width = framebuffer_.Width();
	height = framebuffer_.Height();
}

void Engine::SetWindowSize(const int width, const int height) {
	if (window_) {
		window_->SetClientSize(width, height);
		ApplyFramebufferResize(window_->Width(), window_->Height());
	} else {
		ApplyFramebufferResize(width, height);
	}
}

void Engine::ApplyFramebufferResize(const int width, const int height) {
	if (width <= 0 || height <= 0) {
		return;
	}
	if (framebuffer_.Width() == width && framebuffer_.Height() == height) {
		return;
	}
	framebuffer_.Resize(width, height);
	if (pipelined_ && IsCpuBackend()) {
		framebuffer_alt_.Resize(width, height);
		cpu_frame_ = 0;
		cpu_present_has_prev_ = false;
	}
	if (depth_enabled_) {
		depth_buffer_.Resize(width, height);
	}
	backend_->EnsureSized(width, height);
#ifdef _WIN32
	if (dxgi_present_ && dxgi_presenter_ && window_) {
		if (dxgi_presenter_->Resize(window_->NativeHandle(), width, height)) {
			dxgi_presenter_->SetVsync(vsync_);
			if (direct_present_ && backend_->Name() == "gpu" && dxgi_presenter_->TryRegisterCudaInterop()) {
				backend_->BindDxgiPresenter(dxgi_presenter_.get());
			}
		}
	}
#endif
}

void Engine::EnsureDxgiPresenter() {
#ifdef _WIN32
	if (!dxgi_present_ || window_ == nullptr) {
		return;
	}
	if (dxgi_presenter_ != nullptr && dxgi_presenter_->Valid()) {
		return;
	}
	if (dxgi_presenter_ == nullptr) {
		dxgi_presenter_ = std::make_unique<DxgiPresenter>();
	}
	if (!dxgi_presenter_->Initialize(window_->NativeHandle(), window_->Width(), window_->Height())) {
		dxgi_presenter_.reset();
		return;
	}
	dxgi_presenter_->SetVsync(vsync_);
#endif
}

bool Engine::PresentHostFrame(const std::uint8_t* pixels, const int width, const int height) {
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return false;
	}
#ifdef _WIN32
	if (dxgi_present_) {
		EnsureDxgiPresenter();
		if (dxgi_presenter_ != nullptr && dxgi_presenter_->Valid()) {
			return dxgi_presenter_->CopyFromHostAndPresent(pixels, width, height);
		}
	}
#endif
	if (window_ != nullptr) {
		window_->PresentRaw(pixels, width, height);
		return true;
	}
	return false;
}

void Engine::PresentFramebuffer(const FrameBuffer& framebuffer) {
	(void)PresentHostFrame(framebuffer.Data(), framebuffer.Width(), framebuffer.Height());
}

void Engine::SetCommandBufferReserve(const std::size_t capacity) {
	command_buffer_reserve_ = capacity;
	command_buffer_.Reserve(capacity);
}

void Engine::SetLineSortThreshold(const std::size_t threshold) {
	line_sort_threshold_ = threshold;
}

void Engine::SetVsync(const bool enabled) {
	vsync_ = enabled;
	if (window_) {
		window_->SetVsync(enabled);
	}
#ifdef _WIN32
	if (dxgi_presenter_) {
		dxgi_presenter_->SetVsync(enabled);
	}
#endif
}

bool Engine::VsyncEnabled() const {
	return vsync_;
}

} // namespace hyperlite
