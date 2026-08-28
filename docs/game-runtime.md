# Native game runtime

Optional C++ runtime layered on the existing immediate-mode `Engine`. **Nothing here is required to rasterize.** `Engine` remains the escape hatch.

Python configures and controls. C++ owns runtime state and the hot path. `Game.run()` is a native loop — no Python `while True` and no mandatory per-frame callback.

## Loop

```python
import hyperlite

game = hyperlite.Game(1280, 720, "cpu", "My Game")
game.set_target_fps(60)
game.set_clear_color(20, 20, 30, 255)
game.map_action("quit", hyperlite.Keys.Escape)
game.run()   # native poll / systems / present / pace
```

Headless / tests: `game.set_max_frames(N)` or `HYPERLITE_HEADLESS=1`.

`game.engine()` returns a borrowed `Engine` so you can still draw lines, meshes, and 2D commands yourself.

## Architectural rules (honored)

- Python is never required on the performance-critical path.
- No mandatory Python per-object iteration.
- Systems are registered explicitly; Game never invents hidden ticks (physics, streaming, particles, AI all sit idle until you call them or register a native system that does).
- Hyperlite still owns every pixel (CPU software raster; no Vulkan/GL/D3D/Metal takeover).
- Batch APIs: `draw_mesh_instances`, transform `BatchWorldMatrices`, culler, collision `RaycastBatch`, shader `EvaluateBatch`, particle `Update`.

## C++ surface

Include `engine/runtime/game.hpp`. Subsystems (all optional):

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
| CPU materials | `CpuShader` (bytecode VM, SIMD-friendly scalar lanes) |

Register a native system:

```cpp
void MySystem(hyperlite::Game& game, void*) {
	game.Physics().Step(static_cast<float>(game.DeltaTime()));
}
game.RegisterSystem(&MySystem, nullptr, "physics");
game.Run();
```

## Example

`python/examples/native_game.py`

## Platforms

`Game.run()` is the same on Windows, Linux, and macOS. Platform-specific pieces:

| Piece | Windows | Linux | macOS |
|-------|---------|-------|--------|
| Window | Win32 + GDI/DXGI | X11 MIT-SHM | Cocoa layer blit |
| Gamepad | stub | `/dev/input/js*` | GameController.framework |
| Audio device | Mix-only | Mix-only | Core Audio (`AudioQueue`); `run()` starts it |

Hyperlite still owns pixels on every OS. See [macos.md](macos.md).
