#pragma once

#include "engine/engine.hpp"
#include "engine/runtime/ai.hpp"
#include "engine/runtime/animation.hpp"
#include "engine/runtime/assets.hpp"
#include "engine/runtime/audio.hpp"
#include "engine/runtime/camera.hpp"
#include "engine/runtime/collision.hpp"
#include "engine/runtime/culling.hpp"
#include "engine/runtime/debug_draw.hpp"
#include "engine/runtime/events.hpp"
#include "engine/runtime/input_runtime.hpp"
#include "engine/runtime/job_system.hpp"
#include "engine/runtime/lighting.hpp"
#include "engine/runtime/nav.hpp"
#include "engine/runtime/particles.hpp"
#include "engine/runtime/physics.hpp"
#include "engine/runtime/profiler.hpp"
#include "engine/runtime/resources.hpp"
#include "engine/runtime/save.hpp"
#include "engine/runtime/shader.hpp"
#include "engine/runtime/spatial.hpp"
#include "engine/runtime/streaming.hpp"
#include "engine/runtime/text.hpp"
#include "engine/runtime/transform.hpp"
#include "engine/runtime/ui.hpp"
#include "engine/runtime/world.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Native system callback. Receives the Game and opaque userdata.
 *
 * Must not require Python. Register before Run(); Game never invents systems.
 */
using GameSystemFn = void (*)(class Game&, void*);

/**
 * Optional native game runtime wrapping Engine.
 *
 * Python configures; C++ owns state and the hot path. Engine remains the
 * escape hatch — nothing here is required to rasterize.
 */
class Game {
public:
	Game(
		const int width,
		const int height,
		const BackendKind backend_kind,
		std::string title,
		const PresentMode present_mode = PresentMode::kAuto);

	~Game();

	Game(const Game&) = delete;
	Game& operator=(const Game&) = delete;

	Engine& GetEngine() { return engine_; }
	const Engine& GetEngine() const { return engine_; }

	JobSystem& Jobs() { return jobs_; }
	EventQueue& Events() { return events_; }
	InputRuntime& Input() { return input_; }
	Camera& GetCamera() { return camera_; }
	World& GetWorld() { return world_; }
	Scene& GetScene() { return scene_; }
	ResourceRegistry& Resources() { return resources_; }
	Culler& GetCuller() { return culler_; }
	LightSet& Lights() { return lights_; }
	ShadowMap& Shadows() { return shadows_; }
	ParticleSystem& Particles() { return particles_; }
	Animator& GetAnimator() { return animator_; }
	CollisionWorld& Collision() { return collision_; }
	PhysicsWorld& Physics() { return physics_; }
	SpatialHash& HashGrid() { return hash_; }
	Bvh& GetBvh() { return bvh_; }
	WorldStreamer& Streamer() { return streamer_; }
	NavMesh& Nav() { return nav_; }
	AudioSystem& Audio() { return audio_; }
	BitmapFont& Font() { return font_; }
	Ui& GetUi() { return ui_; }
	AssetManager& Assets() { return assets_; }
	Profiler& GetProfiler() { return profiler_; }
	DebugDraw& Debug() { return debug_; }
	CpuShader& Shaders() { return shaders_; }

	/**
	 * Register a native system. Returns a handle for UnregisterSystem.
	 */
	int RegisterSystem(GameSystemFn fn, void* user, const char* name = "");

	void UnregisterSystem(const int handle);

	/**
	 * Optional per-frame hook (still native). Not required for Run().
	 */
	void SetFrameHook(GameSystemFn fn, void* user);

	void SetTargetFps(const double fps);
	double TargetFps() const { return target_fps_; }

	void SetMaxFrames(const int n) { max_frames_ = n; }
	int MaxFrames() const { return max_frames_; }

	void SetClearColor(const std::uint32_t packed) { clear_packed_ = packed; }
	void SetAutoClear(const bool enabled) { auto_clear_ = enabled; }
	void SetAutoPresent(const bool enabled) { auto_present_ = enabled; }
	void SetApplyCamera(const bool enabled) { apply_camera_ = enabled; }

	void RequestQuit() { quit_ = true; }
	bool QuitRequested() const { return quit_; }

	double DeltaTime() const { return dt_; }
	std::uint64_t FrameIndex() const { return frame_index_; }

	/**
	 * Native main loop: poll, systems, optional clear/present, frame pacing.
	 *
	 * No Python while-True required. Stops on window close, RequestQuit, or max frames.
	 */
	void Run();

	/**
	 * One native frame (useful for tests and hosts that pace themselves).
	 */
	void Step();

	void Shutdown();

	/**
	 * Batch instance draw grouped by mesh. models16 is count * 16 column-major floats.
	 */
	void DrawMeshInstances(
		const int mesh_id,
		const float* models16,
		const std::size_t count,
		const std::uint32_t packed_color);

	void DrawMeshInstancesTextured(
		const int mesh_id,
		const int atlas_id,
		const float* models16,
		const std::size_t count);

	/**
	 * Cull then draw world entities that have mesh + transform (explicit call).
	 */
	int DrawVisibleMeshes(const std::uint32_t fallback_color);

private:
	void BeginNativeFrame();
	void EndNativeFrame();
	void PaceFrame(const std::chrono::steady_clock::time_point start);

	Engine engine_;
	JobSystem jobs_{};
	EventQueue events_{};
	InputRuntime input_{};
	Camera camera_{};
	World world_{};
	Scene scene_{};
	ResourceRegistry resources_{};
	Culler culler_{};
	LightSet lights_{};
	ShadowMap shadows_{};
	ParticleSystem particles_{};
	Animator animator_{};
	CollisionWorld collision_{};
	PhysicsWorld physics_{};
	SpatialHash hash_{};
	Bvh bvh_{};
	WorldStreamer streamer_{};
	NavMesh nav_{};
	AudioSystem audio_{};
	BitmapFont font_{};
	Ui ui_{};
	AssetManager assets_{};
	Profiler profiler_{};
	DebugDraw debug_{};
	CpuShader shaders_{};

	struct SystemSlot {
		int handle = -1;
		GameSystemFn fn = nullptr;
		void* user = nullptr;
		std::string name{};
		bool alive = false;
	};
	std::vector<SystemSlot> systems_{};
	int next_system_ = 1;
	GameSystemFn frame_hook_ = nullptr;
	void* frame_hook_user_ = nullptr;

	double target_fps_ = 0.0;
	int max_frames_ = 0;
	std::uint32_t clear_packed_ = 0xFF14141Au;
	bool auto_clear_ = true;
	bool auto_present_ = true;
	bool apply_camera_ = true;
	bool quit_ = false;
	bool jobs_started_ = false;
	double dt_ = 0.0;
	std::uint64_t frame_index_ = 0;
	std::chrono::steady_clock::time_point last_step_{};
	bool has_last_step_ = false;
};

} // namespace hyperlite
