#include "engine/runtime/game.hpp"

#include "engine/command_buffer.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace hyperlite {

Game::Game(
	const int width,
	const int height,
	const BackendKind backend_kind,
	std::string title,
	const PresentMode present_mode)
	: engine_(width, height, backend_kind, std::move(title), present_mode) {
	int w = 0;
	int h = 0;
	engine_.WindowSize(w, h);
	const float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 16.0f / 9.0f;
	camera_.SetPerspective(1.04719755f, aspect, 0.1f, 100.0f);
	camera_.LookAt({0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
	engine_.SetVsync(false);
}

Game::~Game() {
	Shutdown();
}

int Game::RegisterSystem(GameSystemFn fn, void* user, const char* name) {
	if (fn == nullptr) {
		return -1;
	}
	SystemSlot s{};
	s.handle = next_system_++;
	s.fn = fn;
	s.user = user;
	s.name = name != nullptr ? name : "";
	s.alive = true;
	systems_.push_back(s);
	return s.handle;
}

void Game::UnregisterSystem(const int handle) {
	for (auto& s : systems_) {
		if (s.handle == handle) {
			s.alive = false;
			s.fn = nullptr;
		}
	}
}

void Game::SetFrameHook(GameSystemFn fn, void* user) {
	frame_hook_ = fn;
	frame_hook_user_ = user;
}

void Game::SetTargetFps(const double fps) {
	target_fps_ = std::max(0.0, fps);
}

void Game::Shutdown() {
	quit_ = true;
	audio_.StopOutput();
	jobs_.Shutdown();
	jobs_started_ = false;
}

void Game::BeginNativeFrame() {
	const auto now = std::chrono::steady_clock::now();
	if (has_last_step_) {
		dt_ = std::chrono::duration<double>(now - last_step_).count();
	}
	last_step_ = now;
	has_last_step_ = true;
	profiler_.Begin(Profiler::Frame);
	profiler_.Begin(Profiler::Input);
	engine_.PollEvents();
	input_.PollGamepads();
	input_.BeginFrame(engine_.GetInputState(), &events_);
	events_.Dispatch();
	if (input_.QuitRequested()) {
		quit_ = true;
	}
	profiler_.End(Profiler::Input);
	if (apply_camera_) {
		const Mat4 vp = camera_.ViewProj();
		engine_.SetViewProj(vp.m);
	}
}

void Game::EndNativeFrame() {
	if (auto_present_) {
		engine_.Present();
	}
	profiler_.End(Profiler::Frame);
	++frame_index_;
}

void Game::PaceFrame(const std::chrono::steady_clock::time_point start) {
	if (target_fps_ <= 0.0) {
		return;
	}
	const double target = 1.0 / target_fps_;
	const auto now = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration<double>(now - start).count();
	if (elapsed < target) {
		std::this_thread::sleep_for(std::chrono::duration<double>(target - elapsed));
	}
}

void Game::Step() {
	const auto start = std::chrono::steady_clock::now();
	BeginNativeFrame();
	if (!jobs_started_) {
		jobs_.Start();
		jobs_started_ = true;
	}
	if (auto_clear_) {
		engine_.BeginFrame();
		engine_.PushCommand(MakeDrawCommand(CommandType::kClear, 0, 0, 0, 0, clear_packed_));
	}
	profiler_.Begin(Profiler::Transform);
	world_.Transforms().UpdateDirty();
	profiler_.End(Profiler::Transform);
	for (auto& s : systems_) {
		if (s.alive && s.fn != nullptr) {
			s.fn(*this, s.user);
		}
	}
	if (frame_hook_ != nullptr) {
		frame_hook_(*this, frame_hook_user_);
	}
	if (auto_clear_) {
		engine_.EndFrame();
	}
	EndNativeFrame();
	PaceFrame(start);
}

void Game::Run() {
	quit_ = false;
	audio_.StartOutput();
	while (engine_.IsRunning() && !quit_) {
		if (max_frames_ > 0 && static_cast<int>(frame_index_) >= max_frames_) {
			break;
		}
		Step();
	}
	audio_.StopOutput();
}

void Game::DrawMeshInstances(
	const int mesh_id,
	const float* models16,
	const std::size_t count,
	const std::uint32_t packed_color) {
	if (models16 == nullptr || count == 0U) {
		return;
	}
	engine_.DrawMeshMany(mesh_id, models16, count, packed_color);
	profiler_.Stats().draw_calls += 1;
	profiler_.Stats().visible_meshes += count;
}

void Game::DrawMeshInstancesTextured(
	const int mesh_id,
	const int atlas_id,
	const float* models16,
	const std::size_t count) {
	if (models16 == nullptr || count == 0U) {
		return;
	}
	engine_.DrawMeshTexturedMany(mesh_id, models16, count, atlas_id);
	profiler_.Stats().draw_calls += 1;
	profiler_.Stats().visible_meshes += count;
}

int Game::DrawVisibleMeshes(const std::uint32_t fallback_color) {
	profiler_.Begin(Profiler::Culling);
	const Frustum fr = camera_.MakeFrustum();
	std::vector<CullItem> items;
	std::vector<Entity> ents;
	items.reserve(world_.LiveCount());
	ents.reserve(world_.LiveCount());
	for (const std::uint32_t idx : world_.Live()) {
		const Entity e = world_.FromIndex(idx);
		if (!world_.Enabled(e) || world_.Mesh(e) < 0) {
			continue;
		}
		const TransformId tid = world_.GetTransform(e);
		const Mat4& world = world_.Transforms().WorldMatrix(tid);
		CullItem item{};
		item.mesh_id = world_.Mesh(e);
		item.material_id = world_.Material(e);
		const Vec3 p{world.m[12], world.m[13], world.m[14]};
		item.sphere = {p, 1.0f};
		if (const MeshResource* mr = resources_.GetMesh(item.mesh_id)) {
			item.aabb = TransformAabb(world, mr->bounds);
			item.sphere = {AabbCenter(item.aabb), Length(AabbExtents(item.aabb))};
		}
		items.push_back(item);
		ents.push_back(e);
	}
	std::vector<std::uint32_t> vis(items.size());
	const std::size_t n = culler_.CullFrustum(items.data(), items.size(), fr, vis.data(), vis.size());
	profiler_.Stats().culled_meshes += culler_.LastCulled();
	profiler_.End(Profiler::Culling);
	profiler_.Begin(Profiler::Render);
	int drawn = 0;
	for (std::size_t i = 0; i < n; ++i) {
		const CullItem& item = items[vis[i]];
		const Entity e = ents[vis[i]];
		const Mat4& world = world_.Transforms().WorldMatrix(world_.GetTransform(e));
		std::uint32_t color = fallback_color;
		int atlas = -1;
		if (const Material* mat = resources_.GetMaterial(item.material_id)) {
			color = mat->color;
			atlas = mat->atlas_id;
		}
		if (atlas >= 0) {
			engine_.DrawMeshTextured(item.mesh_id, world.m, atlas);
		} else {
			engine_.DrawMesh(item.mesh_id, world.m, color);
		}
		++drawn;
	}
	profiler_.Stats().draw_calls += static_cast<std::uint64_t>(drawn);
	profiler_.Stats().visible_meshes += static_cast<std::uint64_t>(drawn);
	profiler_.End(Profiler::Render);
	return drawn;
}

} // namespace hyperlite
