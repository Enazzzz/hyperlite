# Native game runtime

Optional C++ runtime layered on the existing immediate-mode `Engine`. **Nothing here is required to rasterize.** `Engine` remains the escape hatch.

Python configures. C++ owns runtime state and the hot path. `Game.run()` is a native loop — no Python `while True` and no mandatory per-frame callback.

## Two ways to write a game

| Path | Who owns the loop | When to use |
|------|-------------------|-------------|
| `Engine` + Python `while engine.is_running()` | Python | Existing games, HUD-heavy prototypes, anything that already poll/draw/presents from Python. See `python/examples/minimal_game.py`. |
| `Game.run()` | C++ | Native systems, batch draws, and frame pacing without a Python loop. Optional `on_frame` for Python HUD. See `python/examples/native_game.py`. |

You can mix them: `game.engine()` is a **borrowed** `Engine` (it does not own the native engine).

## Architecture

```
Game.run()
  └─ while window alive and not RequestQuit / max_frames:
       PollEvents + gamepad          # C++
       InputRuntime edges / events   # C++
       optional camera → SetViewProj
       BeginFrame + Clear            # if auto_clear
       TransformStore.UpdateDirty
       registered native systems     # C++ function pointers only
       optional on_frame hook        # Python takes the GIL here if set
       EndFrame + Present            # if auto_present
       sleep for target FPS          # C++
```

Rules this honors:

- Python is **never required** on the performance-critical path.
- There is **no hidden tick**. Physics, streaming, particles, and AI sit idle until you call them from C++ or register a native system that does.
- Hyperlite still owns every pixel (CPU software raster).
- Batch APIs stay native: `draw_mesh_instances`, transform `BatchWorldMatrices`, culler, collision `RaycastBatch`, shader `EvaluateBatch`, particle `Update`.

### Performance model

| Cost | Where it runs |
|------|----------------|
| Poll, clear, raster, present, FPS cap | C++ inside `Run()` / `Step()` |
| Registered `GameSystemFn` | C++, no Python |
| `on_frame` / Python `while` loop | Python (GIL). Fine for HUD and simple movers; do **not** iterate thousands of objects here |
| `Engine.tick_lines` / `draw_mesh_many` | C++ even when called from Python (one call, many primitives) |

`Game.run()` releases the GIL for the native loop. If you register `on_frame`, each frame re-acquires the GIL for that callback only.

Headless / tests: `game.set_max_frames(N)` or `HYPERLITE_HEADLESS=1`.

## Python surface (what actually exists)

```python
import hyperlite

game = hyperlite.Game(1280, 720, "cpu", "My Game")
game.set_target_fps(60)
game.set_clear_color(20, 20, 30, 255)
game.map_action("quit", hyperlite.Keys.Escape)
game.run()
```

| Method | Role |
|--------|------|
| `run()` / `step()` / `request_quit()` | Native loop |
| `set_target_fps` / `set_max_frames` / `set_clear_color` | Pacing and auto-clear |
| `on_frame(fn)` | Optional Python hook (not required) |
| `engine()` | Borrowed `Engine` |
| `key_down` / `key_pressed` / `key_released` | Input edges |
| `map_action` / `action_down` | Named key bindings |
| `create_entity` / `destroy_entity` | Handle table |
| `draw_mesh_instances` | Batch mesh draw |
| `start_audio_output` / `stop_audio_output` | Core Audio on macOS; no-op elsewhere |
| `profiler_ms()` | Native section timings |
| `delta_time()` / `frame_index()` | Timing |

C++-only (include `engine/runtime/game.hpp`): jobs, world/scene, physics, collision, spatial, streaming, nav, AI, software mixer `Mix()`, bitmap font, immediate UI, save/load, CPU shader VM, lights, shadows, particles, animation.

## C++ surface

```cpp
void MySystem(hyperlite::Game& game, void*) {
	game.Physics().Step(static_cast<float>(game.DeltaTime()));
}
game.RegisterSystem(&MySystem, nullptr, "physics");
game.Run();
```

| Area | Types |
|------|--------|
| Loop / jobs / events / input | `Game`, `JobSystem`, `EventQueue`, `InputRuntime` |
| Math | `Vec2/3/4`, `Mat4`, `Quat`, `Ray`, `Plane`, `Aabb`, `Sphere`, `Frustum` |
| Transforms / camera / world | `TransformStore`, `Camera`, `World`, `Scene`, `Entity` |
| Resources | `ResourceRegistry`, `Material`, `MeshResource`, `TextureRegion` |
| Culling / lights / shadows | `Culler`, `LightSet`, `ShadowMap` |
| Particles / animation | `ParticleSystem`, `Animator` |
| Collision / physics / spatial | `CollisionWorld`, `PhysicsWorld`, `SpatialHash`, `Bvh` |
| Streaming / nav / AI | `WorldStreamer`, `NavMesh`, `StateMachine`, `BehaviorTree` |
| Audio / text / UI | `AudioSystem`, `BitmapFont`, `Ui` |
| Save / assets / tools | `BinaryWriter/Reader`, `AssetManager`, `Profiler`, `DebugDraw` |
| CPU materials | `CpuShader` (bytecode VM) |

## Platforms

`Game.run()` is the same on Windows, Linux, and macOS. See [platforms.md](platforms.md).

| Piece | Windows | Linux | macOS |
|-------|---------|-------|--------|
| Window | Win32 + GDI/DXGI | X11 MIT-SHM | Cocoa layer blit |
| Gamepad | stub | `/dev/input/js*` | GameController.framework |
| Audio device | Mix-only | Mix-only | Core Audio (`AudioQueue`); `run()` starts it |

## Example

`python/examples/native_game.py` — WASD square, Escape to quit, native loop. Headless: `HYPERLITE_HEADLESS=1`.
