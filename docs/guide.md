# Hyperlite — Complete Guide

Everything you need to install, draw, handle input, pick the right backend, tune present paths, and ship a fast 2D game on Hyperlite.

**New here?** Read in this order:

1. [Your first window](#4-your-first-window)
2. [Best practices — read this](#10-best-practices--read-this)
3. [Wireframe & FPS games](#7-wireframe--fps-games)
4. Run `python/examples/minimal_game.py`, then `python/examples/native_game.py`

---

## Table of contents

1. [What Hyperlite is (and is not)](#1-what-hyperlite-is-and-is-not)
2. [Requirements](#2-requirements)
3. [Installation](#3-installation)
4. [Your first window](#4-your-first-window)
5. [The frame loop & command buffer](#5-the-frame-loop--command-buffer)
6. [Drawing — all paths](#6-drawing--all-paths)
7. [Wireframe & FPS games](#7-wireframe--fps-games)
8. [Sprites & atlases](#8-sprites--atlases)
9. [Retained layers (static tilemaps)](#9-retained-layers-static-tilemaps)
10. [Best practices — read this](#10-best-practices--read-this)
11. [CPU vs GPU backend](#11-cpu-vs-gpu-backend)
12. [Building a game — structure & patterns](#12-building-a-game--structure--patterns)
13. [Full API reference](#13-full-api-reference)
14. [Examples](#14-examples)
15. [Performance & profiling](#15-performance--profiling)
16. [Troubleshooting](#16-troubleshooting)
17. [Quick start checklist](#17-quick-start-checklist)

---

## 1. What Hyperlite is (and is not)

Hyperlite is an **immediate-mode 2D renderer** for Python on **Windows, Linux, and macOS**:

- You own the game logic in plain Python.
- Every frame, **you** queue draw operations (`clear`, `line`, `rect_fill`, `draw_sprite`, …).
- The engine rasterizes in **painter’s order** and presents to a Win32, X11, or Cocoa window (or headless for CI).

Think **pygame’s surface + blit model**, stripped to the metal: the `Engine` has no scene graph. Optional C++ runtime (`Game`) adds a native loop, software mixer, bitmap text, and immediate UI — none of that is required to rasterize.

### Good for

- 2D games where you want **control and speed**
- Software-rendered worlds (NumPy / custom raster) + lightweight HUD
- **Wireframe / vector-heavy** games (maze raycasts, debug draw, retro FPS look)
- Sprite-heavy games with atlases and batched draws
- Demoscene-style GPU procedural scenes (spiro benchmarks)
- Performance experiments and teaching rendering

### Not good for (today)

- Full 3D meshes with PBR / scene graph (depth-tested **wireframe**, **filled tris**, and **retained meshes** are Layers 0–2 — see [3d-plan.md](3d-plan.md) / [3d-meshes.md](3d-meshes.md))
- “Drop in PNG and forget” without using `load_atlas` / `blit_rgba`
- Built-in networking

### Mental model

| Layer | You write | Hyperlite does |
|-------|-----------|----------------|
| Game logic | Python and/or C++ `Game` systems | — |
| Draw list | `line`, `draw_sprite`, `upload_frame_rgba`, … | Queues commands |
| Raster | — | CPU SIMD / CUDA |
| Present | — | Win32 GDI/DXGI, X11 (XShm), Cocoa (layer blit), or headless |

---

## 2. Requirements

### Windows

| Component | Required | Notes |
|-----------|----------|-------|
| Windows 10/11 | Yes | Win32 window + DXGI/GDI present |
| Python 3.10+ | Yes | 3.11/3.12 tested; install **per interpreter** |
| Visual Studio 2022 | Yes | “Desktop development with C++” |
| CMake 3.24+ | Yes | On `PATH` |
| CUDA Toolkit | Optional | Required for `"gpu"` backend |
| NumPy | Optional | **Strongly recommended** for wireframe buffers |

### Linux

| Component | Required | Notes |
|-----------|----------|-------|
| g++ (C++20) / CMake 3.24+ | Yes | `build-essential` + `cmake` |
| Python 3.10+ + `python3-dev` | Yes | Needed to build the `.so` extension |
| `libomp-dev` | Recommended | OpenMP for CPU line batches |
| `libx11-dev` `libxext-dev` | Optional | Real window + MIT-SHM blit; without them → headless-only |
| CUDA Toolkit | Optional | `"gpu"` backend |
| Display (`DISPLAY`) | Optional | Unset → auto headless (CI-friendly) |

```bash
sudo apt-get install -y build-essential g++ cmake python3-dev python3-venv \
  libx11-dev libxext-dev libomp-dev
```

### macOS

| Component | Required | Notes |
|-----------|----------|-------|
| macOS 11+ | Yes | Apple Silicon or Intel |
| Xcode Command Line Tools | Yes | `xcode-select --install` |
| CMake 3.24+ | Yes | Homebrew `cmake` |
| Python 3.10+ | Yes | python.org or Homebrew; needed to build the `.so` |
| Homebrew `libomp` | Optional | Apple Clang has no OpenMP by default |
| CUDA Toolkit | No | `"gpu"` is not available; software raster only |

Cocoa presents Hyperlite's host RGBA8 framebuffer (no Metal raster). Details: [macos.md](macos.md).

---

## 3. Installation

Hyperlite is **not on PyPI**. Install from a local clone.

### Windows — Option A (install script)

```powershell
cd C:\path\to\hyperlite
.\scripts\install.ps1
```

Builds Release, runs `pip install`, prepends CUDA to PATH when found.

```powershell
.\scripts\install.ps1 -Python "C:\Path\To\python.exe"
.\scripts\install.ps1 -Editable
.\scripts\install.ps1 -SkipCudaPath
```

### Windows — Option B (pip)

```powershell
python -m pip install . --force-reinstall --user   # Store Python
py -3.11 -m pip install . --force-reinstall      # Programs Python 3.11
```

### Linux

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
python3 -m venv .venv
.venv/bin/pip install .
# or: bash scripts/install.sh
```

Headless CI / no display:

```bash
HYPERLITE_HEADLESS=1 .venv/bin/python -c \
  "import hyperlite; e=hyperlite.Engine(64,64,'cpu',present='headless'); print('OK', e.backend_name())"
```

Native tests without Python:

```bash
ctest --test-dir build --output-on-failure
./build/cpu_line_bench
./build/primitive_bench
```

See [linux-bench.md](linux-bench.md) for measured baseline numbers.

### macOS

```bash
xcode-select --install
brew install cmake python   # if needed
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
python3 -m venv .venv && .venv/bin/pip install .
# or: bash scripts/install.sh
HYPERLITE_HEADLESS=1 .venv/bin/python -c \
  "import hyperlite; e=hyperlite.Engine(64,64,'cpu',present='headless'); print('OK', e.backend_name())"
```

Windowed present uses Cocoa. See [macos.md](macos.md).

### Verify

```powershell
python -c "import hyperlite; e=hyperlite.Engine(64,64,'cpu'); print('OK', hyperlite.__file__, e.backend_name())"
```

```bash
.venv/bin/python -c "import hyperlite; e=hyperlite.Engine(64,64,'cpu',present='headless'); print('OK', hyperlite.__file__, e.backend_name())"
```

### Critical install rules

1. **Same Python for pip and run** — Store `python`, `py -3.10`, and `py -3.11` are separate; install into each you use. On Linux prefer a venv (PEP 668).
2. **Unset `PYTHONPATH`** when testing the installed package — otherwise you may load a stale build artifact.
3. **GPU + DLL/so errors** — add CUDA to `PATH`/`LD_LIBRARY_PATH` or use `backend="cpu"`.
4. **Reinstall after every engine rebuild** — `pip install . --force-reinstall`.
5. **Headless default** — when `DISPLAY` is unset on Linux, present mode is headless automatically. On macOS, Aqua is assumed available; use `HYPERLITE_HEADLESS=1` in CI.

Do **not** rely on `PYTHONPATH=build/` for daily use; that bypasses site-packages and causes version skew.

---

## 4. Your first window

```python
import hyperlite

engine = hyperlite.Engine(800, 600, "cpu", "Hello Hyperlite")

while engine.is_running():
    engine.poll_events()
    if engine.key_down(hyperlite.Keys.Escape):
        break

    engine.begin_frame()
    engine.clear(30, 30, 40, 255)
    engine.line(0, 0, 799, 599, 255, 200, 80, 255)
    engine.end_frame()
    engine.present()
```

Or use the fused helper:

```python
engine.begin_frame()
engine.clear(30, 30, 40, 255)
engine.tick()  # poll_events + end_frame + present
```

Close with **X**, **Escape**, or `break` when `is_running()` is false.

Native loop (C++ owns poll / clear / present; Python is optional):

```python
game = hyperlite.Game(800, 600, "cpu", "Hello Hyperlite")
game.set_target_fps(60)
game.run()  # no Python while-True required
```

Starter games: `python/examples/minimal_game.py` (Python loop) and `python/examples/native_game.py` (`Game.run()`). Platforms and install: [platforms.md](platforms.md). Native architecture: [game-runtime.md](game-runtime.md).

---

## 5. The frame loop & command buffer

### Standard loop

```
while engine.is_running():
    poll_events()           # keyboard / mouse / quit
    update(state, dt)       # your game logic
    begin_frame()           # reset command buffer
    draw(state, engine)     # queue commands
    end_frame()             # rasterize (CPU or GPU)
    present()               # show on screen
```

### What `begin_frame` / `end_frame` actually do

| Step | Effect |
|------|--------|
| `begin_frame()` | Clears the **command buffer** for this frame; starts `record_ms` timer; updates `delta_time()` |
| Draw calls | **Queue** work — nothing hits the screen yet |
| `end_frame()` | Executes all queued commands in order; may sort large blit/line batches |
| `present()` | CPU: blit framebuffer to window. GPU: device→host readback, then blit (unless direct present) |

**Painter’s order is preserved** for clears, vectors, uploads, blits, and sprites.

### Deferred operations (important)

These are **queued at `end_frame`**, not when you call them:

| API | Queued as |
|-----|-----------|
| `clear`, `line`, `rect_fill`, `put_pixel`, … | Command buffer entries |
| `upload_frame_rgba` | `kUploadFrame` command |
| `blit_rgba` | Deferred blit (batched on GPU) |
| `draw_sprite` | Atlas blit (batched on GPU) |
| `lines_bulk` | Batched line raster at `end_frame` |

This fixes compositing bugs (e.g. `blit` then `clear` no longer wipes your blit).

### Fused paths bypass the command buffer

These APIs **do not** use `begin_frame` / `end_frame` — they are one native call:

| API | What it does internally |
|-----|-------------------------|
| `tick()` | poll + end_frame + present |
| `tick_blits()` | poll + clear + sprites + end_frame + present |
| `tick_lines()` | poll + clear + parallel line raster + present |
| `tick_lines_poll()` | same as `tick_lines` (explicit poll in name) |
| `tick_lines_gpu()` | poll + GPU line batch + present |
| `tick_gpu_spiro()` | poll + GPU procedural scene + present |

**Best practice:** use fused paths for hot loops; use manual `begin_frame` when mixing many draw types in one frame.

### `tick()` vs manual steps

```python
engine.tick()  # = poll_events() + end_frame() + present()
```

Use `tick()` when you already called `begin_frame()` and queued draws. For input handling before draw, call `poll_events()` yourself first, then `begin_frame()` → draw → `tick()`.

### `delta_time()`

Seconds since the previous `begin_frame()`. Use for movement:

```python
engine.begin_frame()
dt = engine.delta_time()
player.x += player.vx * dt
```

**Best practice:** use fixed timestep for simulation (see [§10](#fixed-timestep-recommended-for-gameplay)); use `delta_time()` only for interpolation or uncapped benchmarks.

---

## 6. Drawing — all paths

All colors are **RGBA 0–255**. On the **CPU backend**, alpha is fully composited (src-over) for lines, rects, pixels, and RGBA blits. GPU paths may still treat some primitives as opaque — use CPU for wireframe, glow, and translucent HUD.

Coordinates: origin **top-left**, X right, Y down, integer pixels.

### Path A — Vector primitives (lines & rects)

Best for: HUD, grids, debug overlays, minimal games.

```python
engine.clear(20, 24, 32, 255)
engine.line(x0, y0, x1, y1, r, g, b, a=255, width=1)  # width > 1 for thick lines
engine.rect_fill(x, y, w, h, r, g, b, a=200)           # a < 255 blends over background
engine.rect_outline(x, y, w, h, r, g, b, a=255)
```

**Best backend:** `"cpu"` for typical 720p vector workloads (often faster than GPU due to readback tax).

**Best practice:** tens of lines → `line()` in a loop is fine. Hundreds+ → `lines_bulk()` or `tick_lines()`.

### Path B — Full-frame software raster (`upload_frame_rgba`)

Best for: NumPy/custom CPU rasterizer, tile engines, “I already have a pixel buffer”.

```python
import numpy as np

# once at init
xs = np.arange(width, dtype=np.uint32)
ys = np.arange(height, dtype=np.uint32)
y_grid, x_grid = np.meshgrid(ys, xs, indexing="ij")
frame = np.empty(width * height, dtype=np.uint32)

# each frame
engine.begin_frame()
t = frame_index & 255
r = (x_grid + t) & 255
g = (y_grid + t) & 255
b = (x_grid ^ y_grid ^ t) & 255
frame[:] = r | (g << 8) | (b << 16) | np.uint32(0xFF000000)
engine.upload_frame_rgba(frame)
engine.line(hud_x, 0, hud_x, height - 1, 255, 255, 255, 255)  # overlay
engine.tick()
```

**Best backend:** `"gpu"` — upload is ~sub-ms at 720p; overlay vectors on top.

**Do not** fill a buffer with a Python `for y: for x:` loop unless you are benchmarking Python slowness. Use **NumPy** or a C extension.

### Path C — Per-pixel commands (`put_pixel` / `put_pixels` / `put_pixels_buffer`)

Best for: tiny edits, scatter plots, stress tests.

```python
engine.put_pixels(xs, ys, r, g, b, a=255)           # same color, many coords
engine.put_pixels_buffer(xy_pairs, r, g, b, a=255)     # int32 interleaved x,y pairs — faster bulk path
```

**Avoid** full-screen `put_pixel` floods from Python — use Path B or sprites instead.

### Path D — Inline blits (`blit_rgba`)

Best for: small dynamic images, one-off bitmaps not worth atlasing.

```python
engine.blit_rgba(rgba_bytes, dst_x, dst_y, width, height)
```

Deferred and batched on GPU. For **many** repeated blits of the same sheet, use Path E (atlas).

### Path E — Sprites & atlases

See [§8](#8-sprites--atlases).

### Path F — Native bulk commands (`push_commands`)

Advanced: push a packed array of 24-byte `DrawCommand` structs in one call.

```python
# DrawCommand layout: type(u8), pad, x0, y0, x1, y1, packed_color(u32)
engine.push_commands(command_bytes)
```

Use when generating commands from NumPy/C without per-call Python overhead.

### Path G — Wireframe bulk (`lines_bulk` / `tick_lines`)

See [§7](#7-wireframe--fps-games).

### Path H — Zero-copy framebuffer access (`framebuffer_ptr`)

Advanced: get a writable memoryview over the host RGBA8 buffer.

```python
view = engine.framebuffer_ptr()  # memoryview, shape (height, width, 4) or flat — check your binding
```

**Best practice:** only use after `end_frame()` on CPU, or when you know the buffer layout. Prefer `upload_frame_rgba` for full-frame replacement. Useful for readback-free CPU post-processing experiments.

### Composite frames (upload + overlay)

Order in the queue = order on screen:

```python
engine.begin_frame()
engine.upload_frame_rgba(world_buffer)   # background
engine.rect_fill(px, py, 32, 32, ...)    # player on top
engine.line(0, 0, mx, my, 255, 255, 255, 255)  # crosshair
engine.end_frame()
```

---

## 7. Wireframe & FPS games

Wireframe games (maze raycasts, retro vector look, debug overlays) are Hyperlite’s **strongest CPU path**. The engine can raster **millions of line segments per second** when you follow the rules below.

### The golden wireframe recipe

```python
import numpy as np
import hyperlite

WIDTH, HEIGHT = 1280, 720
MAX_LINES = 50_000  # upper bound — allocate once

engine = hyperlite.Engine(WIDTH, HEIGHT, "cpu", "Wireframe FPS")
engine.set_double_buffered_present(True)   # overlap GDI present with next frame raster
engine.set_vsync(True)                     # cap to display refresh; disable for benching

# ── allocate ONCE at load time ──
segments = np.zeros((MAX_LINES, 4), dtype=np.int32)  # x0, y0, x1, y1

while engine.is_running():
    line_count = project_world_to_segments(segments, game_state)  # your raycast / clip

    engine.tick_lines(
        segments[:line_count],          # or pass full buffer if always MAX_LINES
        0, 0, 0, 255,                   # clear color (black)
        0, 255, 80, 255,                # line color (green wireframe)
        width=1,
    )
```

### Why this is fast

| Technique | What it avoids |
|-----------|----------------|
| Preallocated `np.int32` buffer | Per-frame allocation, GC pauses |
| `tick_lines()` | Thousands of Python→C crossings from `line()` loops |
| OpenMP parallel raster | Single-threaded Bresenham bottleneck |
| `set_double_buffered_present(True)` | Main thread blocked on GDI BitBlt + DwmFlush |
| `"cpu"` backend | GPU readback tax (~0.3–20 ms depending on resolution) |

### Performance tiers (line count)

| Lines / frame | Recommended API | Notes |
|---------------|-----------------|-------|
| &lt; 50 | `line()` in loop | Simple, readable |
| 50 – 500 | `lines_bulk()` inside `begin_frame` | Mix with rects/sprites same frame |
| 500 – 100,000+ | `tick_lines()` or `tick_lines_poll()` | Dedicated wireframe frame |
| Per-segment colors | `lines_bulk_colored(segments, colors, …)` | Slightly more bandwidth |
| World-space 3D wireframe | `tick_lines_3d` / `lines_3d` + `enable_depth` | See below |
| World-space filled tris | `tick_tris_3d` / `tris_3d` / `tris_screen` | Layer 1 |
| Retained meshes | `load_mesh` / `draw_mesh` / `tick_mesh` | Layer 2 |

### 3D depth-tested wireframe (Layer 0)

For NULLLIGHT-style FPS wireframe, keep projection on the CPU side of the game, or pass **world-space** segments and let Hyperlite project/clip/depth-test:

```python
import numpy as np
import hyperlite

engine = hyperlite.Engine(1280, 720, "cpu", present="headless")
engine.enable_depth(True)                 # float32 depth plane; clear = 1.0
engine.set_view_proj(view_proj_matrix16)  # column-major float32[16]; identity = world is clip

world = np.zeros((N, 6), dtype=np.float32)  # x0,y0,z0, x1,y1,z1
engine.tick_lines_3d(world[:n], 8, 12, 24, 255, 0, 255, 80, 255, width=1)

# Mixed frame: 3D then 2D HUD (HUD ignores depth)
engine.begin_frame()
engine.clear(0, 0, 0, 255)
engine.lines_3d(world[:n], 0, 255, 80, 255, width=1)
engine.rect_fill(8, 8, 64, 16, 255, 255, 255, 255)
engine.tick()
```

- Clip happens in **homogeneous clip space before** perspective divide (near plane required).
- `lines_3d_screen(segs)` takes pixel `xy` + NDC `z` in `[-1,1]` (skips view-proj).
- `enable_depth(False)` frees the depth plane; 2D paths are unchanged.
- Roadmap: [3d-plan.md](3d-plan.md). Benches: [3d-wireframe-bench.md](3d-wireframe-bench.md).

### 3D filled triangles (Layer 1)

Immediate-mode filled triangles with the same depth / view-proj plumbing as Layer 0. Tris are binned into **64×64** tiles (matching dirty present tiles) and rasterized with half-space coverage + top-left fill rule.

```python
engine.enable_depth(True)
engine.set_view_proj(view_proj_matrix16)
engine.set_cull_backfaces(True)  # default on for world path

tris = np.zeros((N, 9), dtype=np.float32)  # x0,y0,z0, x1,y1,z1, x2,y2,z2
engine.tick_tris_3d(tris[:n], 8, 12, 24, 255, 0, 200, 255, 255)

# Screen-space (pixel xy + NDC z); cull off
engine.tris_screen(screen_tris, 255, 80, 80, 255)
```

- Opaque (`a=255`): depth test + write. Translucent: src-over, **no depth write**.
- World path clips in homogeneous space (near mandatory); a clipped tri can become a quad and is fanned back to tris.
- `set_cull_backfaces(False)` draws both windings on the world path. Screen path never culls.
- Bench numbers: [3d-tri-bench.md](3d-tri-bench.md).

### 3D retained meshes (Layer 2)

Load once, draw many times with a model matrix (like `load_atlas` + `draw_sprite`). Reuses the Layer 1 tiled raster; UVs are stored but texturing is deferred.

```python
# verts: float32 x6 per vert (x,y,z,u,v,_pad); indices: uint32 tris (or None)
mesh = engine.load_mesh(verts, indices)
engine.begin_frame()
engine.clear(8, 12, 20, 255)
engine.draw_mesh(mesh, model16, 0, 200, 255, 255)
engine.tick()
# or: engine.tick_mesh(mesh, model16, 8, 12, 20, 255, 0, 200, 255, 255)
```

- Honors `enable_depth` and `set_cull_backfaces` (same as world `tris_3d`).
- Flat color only in v1 — see [3d-meshes.md](3d-meshes.md). Benches: [3d-mesh-bench.md](3d-mesh-bench.md).

### Mixed frames (wireframe + HUD + sprites)

When one frame needs lines **and** other primitives:

```python
engine.begin_frame()
engine.clear(0, 0, 0, 255)
engine.lines_bulk(segments, 0, 255, 80, 255, width=1)   # parallel at end_frame
engine.rect_fill(10, 10, 200, 24, 0, 0, 0, 180)         # translucent HUD bar
engine.draw_sprite(hud_atlas, 0, 0, 32, 32, 10, 10)
engine.end_frame()
engine.present()
```

**Best practice:** set `set_line_sort_threshold(64)` (default) when many segments share colors — the engine groups same-color runs before raster.

### Double-buffered present

```python
engine.set_double_buffered_present(True)  # alias: set_pipelined(True)
```

| Effect | Detail |
|--------|--------|
| Ping-pong framebuffers | Render frame N while displaying N−1 |
| Async GDI thread | BitBlt runs off the main thread (CPU) |
| +1 frame latency | Do **not** use for input-critical twitch games without measuring |
| Incompatible with | `set_dirty_present(True)`, `set_direct_present(True)` |

Profile with:

```python
raster_ms, present_ms = engine.wireframe_timings()
```

### FPS mouse look

```python
engine.poll_events()
if engine.key_down(hyperlite.Keys.Escape) and engine.mouse_captured():
    engine.set_mouse_captured(False)
elif engine.key_down(hyperlite.Keys.Return) and not engine.mouse_captured():
    engine.set_mouse_captured(True)

if engine.mouse_captured():
    dx, dy = engine.mouse_delta()
    yaw += dx * sensitivity
    pitch = max(-89, min(89, pitch - dy * sensitivity))
```

Capture releases on alt-tab automatically.

### Wireframe demo

```powershell
python python\examples\wireframe_demo.py
```

Shows preallocated NumPy buffer + `tick_lines()` at 1280×720.

---

## 8. Sprites & atlases

### Load once, draw many times

```python
atlas = engine.load_atlas(sheet_rgba_bytes, sheet_width, sheet_height)

engine.begin_frame()
engine.clear(10, 12, 20, 255)
engine.draw_sprite(atlas, src_x, src_y, src_w, src_h, dst_x, dst_y)
engine.end_frame()
```

- `load_atlas` uploads to **CPU and GPU** resident memory (GPU when on `"gpu"` backend).
- Returns an integer **handle** — store it; never reload the same sheet every frame.
- `draw_sprite` queues a deferred atlas blit — batched with other blits at `end_frame`.

### Fused sprite loop (`tick_blits`)

When your frame is **clear + many sprites + present**, one native call replaces dozens of Python crossings:

```python
import array

# 7 × int32 per sprite: atlas_id, src_x, src_y, w, h, dst_x, dst_y
sprites = array.array("i")
for enemy in enemies:
    sprites.extend([atlas, 0, 0, 32, 32, enemy.x, enemy.y])

engine.tick_blits(sprites, clear_r, clear_g, clear_b, a=255)
# internally: poll_events + begin_frame + clear + sprites + end_frame + present
```

**Best practices for sprites:**

1. Build the `array.array("i")` or NumPy buffer **once**, reuse with slice assignment.
2. Sort enemies by atlas id in Python if you have many atlases (engine also sorts at threshold 256).
3. Use `tick_blits()` when the frame is sprite-only.
4. Never call `blit_rgba(unique_bytes)` per sprite — that was the #1 GPU anti-pattern.

### Material sort (large sprite counts)

When a contiguous run of blits/sprites exceeds **256** (default), Hyperlite **stable-sorts by atlas id** before GPU batching — improves cache locality.

```python
engine.set_blit_sort_threshold(256)  # default
engine.set_blit_sort_threshold(64)   # sort sooner (tilemaps with many atlases)
engine.set_blit_sort_threshold(0)    # disable
```

---

## 9. Retained layers (static tilemaps)

Record static geometry **once**, replay every frame without re-running Python draw code.

### Record at load time

```python
engine.begin_frame()
engine.clear(10, 12, 24, 255)
for row in range(rows):
    for col in range(cols):
        engine.draw_sprite(atlas, 0, 0, TILE, TILE, col * TILE, row * TILE)
tilemap_layer = engine.commit_retained_layer()  # captures + clears command buffer
```

### Replay each frame

```python
engine.begin_frame()
engine.draw_retained_layer(tilemap_layer)
engine.draw_sprite(atlas, 0, 0, 32, 32, player.x, player.y)
engine.tick()
```

**Best for:** static tilemaps, pre-baked backgrounds, UI panels that never change.

**Not for:** anything that moves every frame — draw those normally on top.

**Best practice:** one retained layer per static chunk (e.g. per map region), not one giant layer you rebuild often.

See `python/examples/retained_layer_demo.py`.

---

## 10. Best practices — read this

This section is the opinionated core. Follow it unless you have a **measured** reason not to.

### Golden rules

1. **Pick the backend for the workload, not the hype.**

   | What you draw | Use |
   |---------------|-----|
   | Lines, rects, wireframe, minimal HUD | `"cpu"` |
   | NumPy full-frame upload | `"gpu"` |
   | Many atlas sprites | `"cpu"` or `"gpu"` + `tick_blits` |
   | Per-sprite `blit_rgba` with fresh bytes every frame | **Stop** — use atlas |
   | GPU spiro / procedural CUDA demos | `"gpu"` + `tick_gpu_spiro` |
   | Wireframe 10k–50k lines | `"cpu"` + `tick_lines` + double-buffer |

2. **Never fight the command buffer.** Queue in the order you want composited. `clear` first, then world, then entities, then HUD.

3. **Minimize Python → C crossings.** Every `engine.line()` is a Python call. Prefer bulk/fused APIs:

   | Instead of | Use |
   |------------|-----|
   | 10,000× `line()` | `tick_lines()` or `lines_bulk()` |
   | 500× `put_pixel()` | `put_pixels()` or `put_pixels_buffer()` |
   | 200× `draw_sprite()` in hot loop | `tick_blits()` or batch + one `end_frame` |
   | Per-frame `load_atlas()` | Load once at init |

4. **Preallocate buffers at load time.** NumPy segment arrays, sprite `array.array`, atlas handles, retained layers — allocate once, mutate in place.

5. **Use `window_size()` after resize/fullscreen.** Dragging the window border or calling `set_fullscreen` resizes the framebuffer automatically.

6. **Install into the Python you run.** Reinstall after every engine rebuild.

7. **Measure before optimizing.** Use `wireframe_timings()`, `gpu_timings()`, and native benches in `docs/perf-notes.md`.

8. **One present mode at a time.** Double-buffer, dirty partial, and GPU direct present are mutually exclusive — pick one strategy per game.

### The minimal kitchen recipe (recommended default)

For a typical small 2D game:

```python
engine = hyperlite.Engine(960, 540, "cpu", "My Game")

atlas = engine.load_atlas(sheet_bytes, aw, ah)
static_layer = record_tilemap_once(engine, atlas)

while engine.is_running():
    engine.poll_events()
    if engine.key_down(hyperlite.Keys.Escape):
        break

    update(game_state, engine.delta_time())

    engine.begin_frame()
    engine.draw_retained_layer(static_layer)   # static world
    draw_entities(engine, atlas, game_state)   # dynamic sprites
    draw_hud(engine)                           # vectors
    engine.tick()
```

### Game archetype recipes

#### Archetype A — Wireframe FPS

```python
engine = hyperlite.Engine(1280, 720, "cpu", "Wireframe FPS")
engine.set_double_buffered_present(True)
engine.set_mouse_captured(True)
segments = np.zeros((MAX_LINES, 4), dtype=np.int32)

while engine.is_running():
    update_player(input, dt)
    n = cast_rays(segments, world, player)
    engine.tick_lines(segments[:n], 0,0,0,255, 0,255,80,255, width=1)
```

#### Archetype B — Tilemap + entities

```python
engine = hyperlite.Engine(960, 540, "cpu", "Roguelike")
tile_layer = build_retained_tilemap(engine, atlas)
while engine.is_running():
    engine.poll_events()
    update(world)
    engine.begin_frame()
    engine.draw_retained_layer(tile_layer)
    for e in entities: e.draw(engine, atlas)
    draw_hud(engine)
    engine.tick()
```

#### Archetype C — NumPy software renderer + HUD

```python
engine = hyperlite.Engine(1280, 720, "gpu", "Software Raycaster")
engine.set_direct_present(True)   # skip readback — GPU owns the swapchain
frame = np.zeros(W * H, dtype=np.uint32)

while engine.is_running():
    engine.poll_events()
    render_world_numpy(frame, game_state)
    engine.begin_frame()
    engine.upload_frame_rgba(frame)
    draw_minimap(engine)
    engine.tick()
```

#### Archetype D — Sprite bullet hell

```python
engine = hyperlite.Engine(1280, 720, "cpu", "Shmup")
sprites = array.array("i", [0] * (MAX_SPRITES * 7))
engine.set_blit_sort_threshold(128)

while engine.is_running():
    fill_sprite_buffer(sprites, bullets, enemies)
    engine.tick_blits(sprites, 5, 5, 15, 255)
```

### Anti-patterns (do not do this)

| Anti-pattern | Why it hurts | Do instead |
|--------------|--------------|------------|
| `backend="gpu"` for line/rect-heavy 720p game | ~0.3 ms kernel + **~15–20 ms readback/present** | `"cpu"` |
| 200× `blit_rgba(unique_bytes)` per frame on GPU | Upload storm; cache misses | `load_atlas` + `draw_sprite` |
| Full-screen Python nested loops for pixels | ~40 ms `record_ms` | NumPy + `upload_frame_rgba` |
| `clear()` after `blit_rgba` expecting blit to show | Bad compositing order | `clear` first, then draw |
| `put_pixel` for 32×32 sprite | 1024 Python calls | `draw_sprite` or one `blit_rgba` |
| 50,000× `line()` per frame | Python overhead dominates | `tick_lines()` |
| `np.zeros()` every frame for segments | GC + allocation | Preallocate once, slice-write |
| `set_pipelined(True)` in input-sensitive game | +1 frame input latency | Default off; measure feel |
| `set_dirty_present(True)` + double-buffer | Incompatible present paths | Pick one |
| Stale `PYTHONPATH=build/Release` | Wrong `.pyd`, missing APIs | Installed site-packages only |
| Reloading atlases each frame | Redundant upload | Store handle at init |
| `tick_lines()` mixed with `begin_frame` same frame | Undefined — fused path owns the frame | Use `lines_bulk` inside manual frame |

### Backend decision tree

```
Start
 │
 ├─ Wireframe / 1k–100k lines per frame?
 │    └─ YES → backend="cpu", tick_lines(), set_double_buffered_present(True)
 │
 ├─ Full-screen NumPy/custom raster every frame?
 │    └─ YES → backend="gpu", upload_frame_rgba, set_direct_present(True)
 │
 ├─ Mostly lines/rects/HUD (< few thousand primitives)?
 │    └─ YES → backend="cpu", tick() or manual loop
 │
 ├─ Hundreds/thousands of atlas sprites?
 │    ├─ Need max FPS + GPU → backend="gpu", tick_blits(), set_direct_present(True)
 │    └─ Simpler / CPU wins → backend="cpu", draw_sprite loop or tick_blits
 │
 └─ CUDA procedural demo (spiro)?
      └─ backend="gpu", tick_gpu_spiro()
```

### Present path selection

| Mode | When | API | Latency |
|------|------|-----|---------|
| Headless | CI / no display / benches | `present="headless"` or `HYPERLITE_HEADLESS=1` | n/a |
| Windowed (auto) | Interactive apps | default `present="auto"` | 0 frames |
| Default CPU present | General CPU games | automatic | 0 frames |
| CPU double-buffer | Wireframe at high line counts | `set_double_buffered_present(True)` | +1 frame |
| GPU readback + present | `"gpu"`, general | default `present()` | 0 frames |
| GPU direct (no CPU readback) | Windows + CUDA→DXGI | `set_direct_present(True)` | 0 frames |
| GPU pipelined readback | GPU throughput > latency | `set_pipelined(True)` | +1 frame |
| Partial CPU present | Windows GDI dirty regions | `set_dirty_present(True)` | 0 frames |

**Rules:**

- Double-buffer and dirty partial **cannot** combine.
- Double-buffer and GPU direct **cannot** combine.
- For wireframe FPS: double-buffer on CPU. For GPU sprite games on Windows: direct present.
- On Linux, windowed present uses X11 + MIT-SHM when available; otherwise headless. DXGI/GDI APIs remain Windows-only no-ops/stubs on Linux.
- On macOS, windowed present blits host RGBA8 into an AppKit layer (nearest-neighbor). CUDA/DXGI are unavailable. Raster stays CPU software; see [macos.md](macos.md).

### Python performance patterns

```python
# ✅ GOOD — mutate preallocated buffer
segments[row:row+n] = new_segments

# ❌ BAD — allocates every frame
segments = np.array([...], dtype=np.int32)

# ✅ GOOD — local refs in hot loop
tick = engine.tick_lines
while engine.is_running():
    fill(segments)
    tick(segments, 0,0,0,255, 0,255,80,255)

# ❌ BAD — attribute lookup every iteration in tight inner code (minor but adds up)
while engine.is_running():
    engine.tick_lines(...)
```

**Best practice:** keep hot-loop Python minimal — projection/physics in NumPy vectorized ops where possible; one fused native call per frame for draw.

### Buffer layout reference

| Buffer | Layout | dtype |
|--------|--------|-------|
| Line segments | `(N, 4)` → x0, y0, x1, y1 | `int32` |
| Line colors (bulk colored) | `(N,)` packed RGBA | `uint32` |
| Sprites (`tick_blits`) | flat `7 × N` int32 | `int32` |
| Put pixels buffer | flat `2 × N` int32 x,y pairs | `int32` |
| Full frame upload | `W × H × 4` RGBA8 or packed uint32 | `uint8` / `uint32` |
| DrawCommand push | 24 bytes per command | raw bytes |

### Alpha & compositing

- CPU: src-over alpha on lines, rects, pixels, blits.
- Draw **back to front** for correct transparency.
- Use `a=255` for opaque HUD; lower alpha for overlays.
- Thick lines (`width > 1`) on CPU use filled bbox raster — fast for opaque, correct blend for alpha.

### Layer draw order

Always queue **back to front**:

```python
engine.begin_frame()
engine.clear(...)
engine.draw_retained_layer(bg_layer)   # 1. static world
draw_ground_entities(engine)           # 2. gameplay
draw_player(engine)                    # 3. player
draw_fx(engine)                        # 4. particles
draw_hud(engine)                       # 5. UI
engine.end_frame()
```

### Fixed timestep (recommended for gameplay)

```python
FIXED_DT = 1.0 / 60.0
accum = 0.0

while engine.is_running():
    engine.poll_events()
    accum += engine.delta_time()

    while accum >= FIXED_DT:
        world.update(FIXED_DT)
        accum -= FIXED_DT

    alpha = accum / FIXED_DT   # optional interpolation factor for render
    engine.begin_frame()
    world.draw(engine, alpha)
    engine.tick()
```

Decouple simulation from render rate — physics uses `FIXED_DT`, not variable `delta_time()`.

### Frame budget worksheet (720p @ 60 FPS = 16.7 ms)

| Stage | CPU wireframe target | GPU upload target |
|-------|---------------------|-------------------|
| Python game logic | &lt; 2 ms | &lt; 2 ms |
| Raster / kernel | &lt; 4 ms | &lt; 1 ms |
| Readback | 0 (CPU) | 0 with direct present |
| Present | &lt; 2 ms with double-buffer | &lt; 1 ms direct |
| **Headroom** | ~8 ms | ~12 ms |

If present dominates, enable double-buffer (CPU) or direct present (GPU).

### Command buffer tuning

```python
engine.set_command_buffer_reserve(1 << 16)  # default 65536 — raise if pushing 100k+ commands
engine.set_line_sort_threshold(64)        # group wireframe by color
engine.set_blit_sort_threshold(256)       # group sprites by atlas
```

Set thresholds to **0** to disable sorting when profiling shows sort cost &gt; benefit (rare).

### Vsync & frame pacing

```python
engine.set_vsync(True)   # default — DwmFlush (CPU) or DXGI sync interval (GPU direct)
```

- **Benchmarking:** disable vsync to see raw throughput.
- **Shipping:** enable vsync to avoid tearing and reduce CPU burn.
- **Cap without vsync:** sleep in Python (see `minimal_game.py`).

### Initialization checklist

At game startup, do all of this **once**:

- [ ] `Engine(width, height, backend, title)`
- [ ] `load_atlas()` for all sprite sheets
- [ ] `commit_retained_layer()` for static maps
- [ ] Preallocate NumPy / array buffers
- [ ] Configure present mode (`set_double_buffered_present`, `set_direct_present`, etc.)
- [ ] `set_command_buffer_reserve()` if expecting huge command counts
- [ ] `set_mouse_captured(True)` if FPS controls

### Compared to pygame (what we learned from benchmarks)

On the same hardware, Hyperlite **wins** when used as designed:

| Workload | Hyperlite | pygame |
|----------|-----------|--------|
| NumPy upload + HUD | ~680 FPS (GPU upload path) | ~348 FPS |
| Vector grid + crosshair 960×540 | ~3,200 FPS (CPU) | ~2,360 FPS |
| 200× 32×32 sprite blits | ~3,200 FPS (CPU atlas) | ~1,410 FPS |
| 1000× sprite blits | ~1,900 FPS (CPU) | ~635 FPS |
| 50k wireframe lines 1280×720 | ~30–60+ FPS (CPU tick_lines) | Not comparable |

pygame still wins on **ecosystem** (fonts, mixer, transforms, cross-platform) — not raw throughput for these paths.

### Migrating from pygame

| pygame | Hyperlite |
|--------|-----------|
| `pygame.display.set_mode()` | `Engine(w, h, backend, title)` |
| `screen.fill()` | `clear()` or `tick_lines` clear args |
| `pygame.draw.line()` | `line()` or `lines_bulk()` / `tick_lines()` |
| `screen.blit()` | `draw_sprite()` / `blit_rgba()` |
| `pygame.event.get()` | `poll_events()` + `key_down()` / `mouse_*` |
| `clock.tick(60)` | `set_vsync(True)` or manual sleep |
| `Surface.get_at()` | No direct equivalent — use your own buffers |

---

## 11. CPU vs GPU backend

```python
engine = hyperlite.Engine(1280, 720, "gpu", "Title")
print(engine.backend_name())  # "gpu" or "cpu" if GPU init failed
```

### CPU backend (`"cpu"`)

- Rasterizes directly into host framebuffer (SIMD alpha, OpenMP line batches).
- **No readback tax** — pixels are already on CPU.
- Present via persistent DIB + BitBlt (+ optional async double-buffer).
- **Default choice** for vector games, wireframe, sprite games at 720p–1080p.

### GPU backend (`"gpu"`)

- Commands execute on CUDA device; default `present()` readbacks to CPU then GDI blits.
- **Wins** when GPU work dominates: full-frame upload, batched atlas blits, spiro scenes, `tick_lines_gpu`.
- **Loses** for small vector-only scenes due to readback + present overhead.

### GPU configuration knobs

```python
engine.set_direct_present(True)              # DXGI swapchain — skip CPU readback (GPU only)
engine.set_pipelined(True)                   # double-buffer: GPU pipelined readback (+1 frame)
engine.set_double_buffered_present(True)     # same as set_pipelined
engine.set_blit_sort_threshold(256)
engine.set_dirty_present(True)               # CPU partial present — not with double-buffer
```

### GPU procedural / benchmark API

| Method | Description |
|--------|-------------|
| `supports_gpu_scene()` | Device-native scene available |
| `clear_gpu(r, g, b, a)` | Direct device clear |
| `spiro_scene_cuda(...)` | GPU spiro benchmark |
| `spiro_frame_direct(...)` | Fused clear + scene |
| `tick_gpu_spiro(...)` | Poll + scene + present in one call |
| `tick_lines_gpu(...)` | Poll + GPU line batch + present |
| `gpu_timings()` | `(record_ms, upload_ms, kernel_ms, readback_ms, present_ms)` |

---

## 12. Building a game — structure & patterns

Hyperlite does not impose architecture. A layout that scales:

```
my_game/
  main.py              # window, loop, backend config, present mode
  assets.py            # load_atlas calls
  layers.py            # commit_retained_layer for tilemaps
  buffers.py           # preallocated NumPy segment/sprite buffers
  game/
    player.py
    world.py
    input.py
    raycast.py         # fill segments buffer
  render/
    draw_world.py
    draw_hud.py
```

### Entity pattern

```python
from dataclasses import dataclass

@dataclass
class Entity:
    x: float
    y: float

    def update(self, dt: float, input_state) -> None:
        ...

    def draw(self, engine: hyperlite.Engine, atlas: int) -> None:
        engine.draw_sprite(atlas, 0, 0, 32, 32, int(self.x), int(self.y))
```

### Input helper

```python
def read_input(engine: hyperlite.Engine) -> tuple[int, int]:
    dx = dy = 0
    if engine.key_down(hyperlite.Keys.W): dy -= 1
    if engine.key_down(hyperlite.Keys.S): dy += 1
    if engine.key_down(hyperlite.Keys.A): dx -= 1
    if engine.key_down(hyperlite.Keys.D): dx += 1
    return dx, dy
```

### Collision (AABB)

```python
def rects_overlap(ax, ay, aw, ah, bx, by, bw, bh) -> bool:
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
```

### Minimal game template

See `python/examples/minimal_game.py` — uses **`backend="cpu"`** (correct for vector-heavy demo), WASD, grid, crosshair, frame cap.

---

## 13. Full API reference

### Constructor

```python
hyperlite.Engine(width, height, backend="cpu", title="Hyperlite", present="auto")
```

| Arg | Type | Description |
|-----|------|-------------|
| `width`, `height` | int | Framebuffer size in pixels |
| `backend` | `"cpu"` \| `"gpu"` | Rendering backend |
| `title` | str | Window title |
| `present` | `"auto"` \| `"headless"` \| `"window"` | Present surface (auto → headless when no display) |

Env overrides: `HYPERLITE_HEADLESS=1`, `HYPERLITE_PRESENT=headless|window`.

### Frame lifecycle

| Method | Returns | Description |
|--------|---------|-------------|
| `poll_events()` | None | Pump OS events (Win32/X11/Cocoa); updates input; handles resize |
| `begin_frame()` | None | Start command recording; reset `delta_time` anchor |
| `end_frame()` | None | Execute command buffer |
| `present()` | None | Show framebuffer (no-op when headless) |
| `tick()` | None | `poll_events` + `end_frame` + `present` |
| `is_running()` | bool | Window/session alive and not quit |
| `delta_time()` | float | Seconds since last `begin_frame` |

### Drawing (queued — require `begin_frame` … `end_frame`)

| Method | Description |
|--------|-------------|
| `clear(r, g, b, a=255)` | Full-frame clear |
| `put_pixel(x, y, r, g, b, a=255)` | Single pixel |
| `put_pixels(xs, ys, r, g, b, a=255)` | Bulk pixels (same color) |
| `put_pixels_buffer(xy_pairs, r, g, b, a=255)` | Bulk pixels from int32 x,y pairs |
| `line(x0, y0, x1, y1, r, g, b, a=255, width=1)` | Line segment; thick lines on CPU |
| `rect_fill(x, y, w, h, r, g, b, a=255)` | Filled rectangle |
| `rect_outline(x, y, w, h, r, g, b, a=255)` | Rectangle border |
| `upload_frame_rgba(buffer)` | Full RGBA8 upload (deferred) |
| `blit_rgba(buffer, x, y, w, h)` | Inline RGBA blit (deferred) |
| `push_commands(buffer)` | Packed `DrawCommand` array (24 bytes each) |
| `lines_bulk(segments, r, g, b, a=255, width=1)` | Queue N×4 int32 segments |
| `lines_bulk_colored(segments, colors, width=1)` | Per-segment packed RGBA colors |

### Fused frame APIs (own the full frame — no manual begin/end)

| Method | Description |
|--------|-------------|
| `tick_blits(sprite_buffer, r, g, b, a=255)` | Poll + clear + sprites + present |
| `tick_lines(segments, cr, cg, cb, ca, r, g, b, a=255, width=1)` | Poll + clear + parallel CPU lines + present |
| `tick_lines_poll(...)` | Same as `tick_lines` |
| `tick_lines_gpu(...)` | Poll + clear + GPU lines + present |
| `enable_depth(bool)` / `depth_enabled()` | Allocate/free float32 depth plane |
| `set_view_proj(matrix16)` | Column-major world→clip (16 float32) |
| `tick_lines_3d(world_segs, cr..ca, r..a, width=1)` | Poll + clear color/depth + 3D lines + present |
| `lines_3d(world_segs, r, g, b, a=255, width=1)` | Flush pending 2D, draw world-space 3D lines |
| `lines_3d_screen(segs, ...)` | Pixel xy + NDC z `[-1,1]` (float32×6); skips view-proj |
| `set_cull_backfaces(bool)` / `cull_backfaces()` | World-space triangle backface cull (default on) |
| `tick_tris_3d(world_verts, cr..ca, r..a)` | Poll + clear color/depth + filled tris + present |
| `tris_3d(world_verts, r, g, b, a=255)` | Flush pending 2D, draw world-space filled triangles |
| `tris_screen(screen_verts, r, g, b, a=255)` | Pixel xy + NDC z `[-1,1]` (float32×9); cull off |
| `load_mesh(verts, indices=None)` | → int handle (float32×6/vert; uint32 indices optional) |
| `draw_mesh(mesh, model16, r, g, b, a=255)` | Draw retained mesh with column-major model |
| `tick_mesh(mesh, model16, cr..ca, r..a)` | Poll + clear + draw_mesh + present |
| `tick_gpu_spiro(...)` | Poll + GPU spiro scene + present |

### Sprites & layers

| Method | Description |
|--------|-------------|
| `load_atlas(buffer, w, h)` | → int handle |
| `draw_sprite(atlas, sx, sy, sw, sh, dx, dy)` | Atlas sub-rect blit |
| `commit_retained_layer()` | → int layer handle |
| `draw_retained_layer(handle)` | Replay frozen layer (command buffer) |
| `draw_retained_layer_gpu(handle)` | Replay on device (GPU path) |

**Buffer layouts:**

- `sprite_buffer`: **7 × int32 per sprite** — `(atlas_id, src_x, src_y, width, height, dst_x, dst_y)`
- `segments`: **4 × int32 per line** — `(x0, y0, x1, y1)`
- `world_segs` / 3D screen segs: **6 × float32 per line** — `(x0,y0,z0, x1,y1,z1)`
- `world` / screen tris: **9 × float32 per triangle** — `(x0,y0,z0, x1,y1,z1, x2,y2,z2)`
- mesh verts: **6 × float32 per vertex** — `(x,y,z,u,v,_pad)`; indices: **uint32** (3 per tri)

### Advanced / introspection

| Method | Description |
|--------|-------------|
| `framebuffer_ptr()` | Writable memoryview over host RGBA8 |
| `wireframe_timings()` | `(raster_ms, present_ms)` from last fused wireframe frame |
| `gpu_timings()` | `(record_ms, upload_ms, kernel_ms, readback_ms, present_ms)` |

### Performance / present tuning

| Method | Description |
|--------|-------------|
| `set_blit_sort_threshold(n)` | Material sort cutoff (0=off, default 256) |
| `set_line_sort_threshold(n)` | Line color/width sort cutoff (0=off, default 64) |
| `set_command_buffer_reserve(n)` | Pre-reserve command capacity (default 65536) |
| `set_vsync(enabled)` | Vertical sync — DwmFlush (CPU) / sync interval (DXGI) |
| `vsync_enabled()` | bool |
| `set_direct_present(enabled)` | GPU DXGI present, no CPU readback |
| `set_pipelined(enabled)` | Double-buffered present (CPU async GDI or GPU pipelined readback) |
| `set_double_buffered_present(enabled)` | Alias for `set_pipelined` |
| `set_dirty_present(enabled)` | CPU partial present for dirty 64px tiles |

### Input

| Method | Returns |
|--------|---------|
| `key_down(vk_code)` | bool — use `hyperlite.Keys.*` |
| `mouse_pos()` | `(x, y)` client-space position |
| `set_mouse_captured(enabled)` | None — hide cursor, warp to center, relative motion |
| `mouse_captured()` | bool |
| `mouse_delta()` | `(dx, dy)` since last `poll_events` (use while captured) |
| `mouse_button_down(button)` | bool — use `hyperlite.MouseButtons.*` |

Common keys: `Keys.Escape`, `Keys.W/A/S/D`, `Keys.F11`, `Keys.Return` (see `hyperlite.Keys` in Python).

### Window

| Method | Returns |
|--------|---------|
| `set_fullscreen(enabled)` | None — borderless fullscreen on active monitor |
| `is_fullscreen()` | bool |
| `window_size()` | `(width, height)` — current framebuffer / client size |
| `set_window_size(w, h)` | None — resize window and framebuffer |

**Resize:** Drag the window border — `poll_events()` resizes the framebuffer automatically. Or call `set_window_size(1280, 720)` / `set_fullscreen(True)`.

Capture releases automatically on alt-tab (`WM_KILLFOCUS`).

### Info

| Method | Returns |
|--------|---------|
| `backend_name()` | `"cpu"` or `"gpu"` |

### GPU scene / spiro

| Method | Notes |
|--------|-------|
| `supports_gpu_scene()` | bool |
| `clear_gpu(r, g, b, a=255)` | Device clear |
| `spiro_object(...)` | CPU command path |
| `spiro_scene(...)` / `spiro_scene_fast(...)` | CPU command path |
| `spiro_scene_cuda(...)` | GPU |
| `spiro_frame_cuda(...)` | GPU graph |
| `spiro_frame_direct(...)` | GPU fused |
| `tick_gpu_spiro(...)` | Poll + GPU scene + present |

### Timing fields

```python
record_ms, upload_ms, kernel_ms, readback_ms, present_ms = engine.gpu_timings()
raster_ms, present_ms = engine.wireframe_timings()
```

| Field | Meaning |
|-------|---------|
| `record_ms` | Python + command recording (`begin_frame` → `end_frame`) |
| `upload_ms` | Host→device staging |
| `kernel_ms` | CUDA kernel execution |
| `readback_ms` | Device→host copy (0 with direct present) |
| `present_ms` | Wall time for `present()` path |
| `raster_ms` | Wireframe raster only (`tick_lines` path) |

---

## 14. Examples

| File | Demonstrates |
|------|--------------|
| `minimal_game.py` | **Start here** — WASD Python loop, CPU backend |
| `native_game.py` | **Native loop** — same mover via `Game.run()` |
| `wireframe_demo.py` | **Wireframe FPS pattern** — NumPy segments + `tick_lines` |
| `window_input_test.py` | Mouse lock, fullscreen, live resize |
| `software_raster_demo.py` | NumPy + `upload_frame_rgba` + HUD |
| `retained_layer_demo.py` | Retained tilemap + `tick_blits` |
| `pixel_stress.py` | Command-buffer stress (`--mode`, `--backend`, `--path`) |
| `uncapped_object_bench.py` | GPU spiro throughput |
| `capped_usage_bench.py` | Fixed FPS telemetry |
| `hyperlite_compare_bench.py` | Internal Hyperlite comparisons |
| `pygame_compare_bench.py` | Head-to-head vs pygame |

```powershell
python python\examples\minimal_game.py
python python\examples\wireframe_demo.py
python python\examples\window_input_test.py
python python\examples\software_raster_demo.py --fill numpy
python python\examples\retained_layer_demo.py
python python\examples\pixel_stress.py --mode kitchen --backend cpu
```

---

## 15. Performance & profiling

### Workflow

1. **Pick the right API tier** (see [§7 performance tiers](#performance-tiers-line-count)).
2. **Enable the right present mode** (see [present path selection](#present-path-selection)).
3. **Profile in-game** with timing APIs.
4. **Confirm with native benches** if still stuck.

### Native benchmarks (C++)

```powershell
.\build\Release\reference_render_tests.exe   # CPU/GPU parity + alpha
.\build\Release\cpu_line_bench.exe             # raw line throughput
.\build\Release\gpu_blit_bench.exe             # sprite batch sweep
.\build\Release\primitive_bench.exe              # put-pixel / rect sweeps
```

### In-game profiling

```python
# Wireframe path
raster_ms, present_ms = engine.wireframe_timings()

# General / GPU path
record_ms, upload_ms, kernel_ms, readback_ms, present_ms = engine.gpu_timings()
```

### Interpretation guide

| Symptom | Likely cause | Fix |
|---------|--------------|-----|
| High `record_ms` | Python draw loop | Bulk APIs, NumPy, retained layers, `tick_*` |
| High `raster_ms` | Too many lines unsorted | `lines_bulk`, reduce segments, clip off-screen |
| High `present_ms` on CPU | GDI blocking | `set_double_buffered_present(True)` |
| High `readback_ms` on GPU | Default present path | `set_direct_present(True)` or switch to `"cpu"` |
| High `kernel_ms` + low line count on GPU | GPU wrong tool for vectors | Use `"cpu"` |
| FPS fine but input laggy | Double-buffer enabled | Disable pipelined present |

See also `docs/perf-notes.md` and `docs/uncapped-bench-optimization-log.md`.

---

## 16. Troubleshooting

### `ImportError: DLL load failed while importing hyperlite`

```powershell
python -m pip install . --force-reinstall --user
# ensure CUDA bin on PATH, or:
engine = hyperlite.Engine(..., "cpu", ...)
```

Unset `PYTHONPATH`. Verify: `python -c "import hyperlite; print(hyperlite.__file__)"`.

### `AttributeError: module 'hyperlite' has no attribute 'tick_lines'` (or similar)

Stale install or wrong Python. Reinstall into **the same interpreter** you run.

### Window black

- Call `begin_frame()` before draws, `end_frame()` before `present()` (unless using fused `tick_*`).
- Queue `clear` or `upload_frame_rgba` **after** `begin_frame`.
- First frame with double-buffer may show nothing until frame 2 — expected.

### `backend_name()` is `"cpu"` but I asked for `"gpu"`

CUDA missing, no NVIDIA GPU, or context init failed — silent fallback.

### Kitchen mode / stress looks corrupted on GPU

Update and reinstall. Ensure latest `cuda_context.cu` (parallel pixels, sequential vectors, batched blits).

### Low FPS with sprites on GPU

1. Use `load_atlas` + `draw_sprite`, not repeated `blit_rgba` with new bytes.
2. Try `tick_blits()` for whole-frame sprite passes.
3. Enable `set_direct_present(True)`.
4. Compare against `"cpu"` — often wins for mixed scenes.

### Low FPS with wireframe on CPU

1. Use `tick_lines()`, not `line()` loops.
2. Preallocate segment buffer — no per-frame `np.array()`.
3. Enable `set_double_buffered_present(True)`.
4. Check `wireframe_timings()` — if `present_ms` dominates, double-buffer helps; if `raster_ms` dominates, reduce line count or clip.

### Game feels laggy

- Disable `set_pipelined(True)` / `set_double_buffered_present(True)` for input-sensitive games.
- Cap frame rate in Python if spinning uncapped burns CPU (see `minimal_game.py`).

### Resize broke my layout

Call `window_size()` each frame after `poll_events()`, or handle resize event once and rebuild retained layers if dimensions changed significantly.

---

## 17. Quick start checklist

1. `.\scripts\install.ps1` or `python -m pip install . --force-reinstall --user`
2. Run `python/examples/minimal_game.py`, then `native_game.py`, then `wireframe_demo.py`
3. Pick backend from [§10 decision tree](#backend-decision-tree)
4. **Wireframe game?** → `"cpu"` + preallocated NumPy + `tick_lines` + double-buffer
5. **Sprite game?** → atlas + retained layers for static world, `tick_blits` or batched sprites
6. **NumPy raster?** → `"gpu"` + `upload_frame_rgba` + `set_direct_present(True)`
7. Profile with `wireframe_timings()` / `gpu_timings()` before micro-optimizing
8. Reinstall after every engine rebuild

---

Hyperlite stays out of your way — your Python **is** the game engine. Draw explicitly, batch aggressively, preallocate buffers, pick the present path for your workload, and measure often.
