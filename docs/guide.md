# Hyperlite — How to Use

Complete guide for installing, drawing, handling input, using the GPU backend, and structuring a game on top of Hyperlite.

---

## Table of contents

1. [What Hyperlite is (and is not)](#1-what-hyperlite-is-and-is-not)
2. [Requirements](#2-requirements)
3. [Installation](#3-installation)
4. [Your first window](#4-your-first-window)
5. [The frame loop](#5-the-frame-loop)
6. [Drawing](#6-drawing)
7. [Input](#7-input)
8. [CPU vs GPU backend](#8-cpu-vs-gpu-backend)
9. [Building a game — patterns & structure](#9-building-a-game--patterns--structure)
10. [API reference](#10-api-reference)
11. [Examples](#11-examples)
12. [Performance tips](#12-performance-tips)
13. [Troubleshooting](#13-troubleshooting)

---

## 1. What Hyperlite is (and is not)

Hyperlite is an **immediate-mode** renderer exposed to Python:

- You create a window and a framebuffer.
- Every frame, **you** issue draw commands (`clear`, `line`, `rect_fill`, …).
- The engine rasterizes those commands and presents the result.

Think **pygame without the kitchen sink** — no built-in sprites, fonts, audio, physics, or scene graph. That keeps the core tiny and fast; you build game logic in plain Python (or your own layers on top).

**Good for:** prototypes, visual simulations, demoscene-style effects, learning rendering, performance experiments, games where you want full control.

**Not (yet) good for:** 3D, textured sprites out of the box, cross-platform macOS/Linux (Windows only today), editor tooling.

---

## 2. Requirements

| Component | Required | Notes |
|-----------|----------|-------|
| Windows 10/11 | Yes | Win32 window + input |
| Python 3.10+ | Yes | 3.11 tested |
| Visual Studio 2022 | Yes | “Desktop development with C++” workload |
| CMake 3.24+ | Yes | On `PATH` |
| CUDA Toolkit | Optional | NVIDIA GPU backend; CPU works without it |

---

## 3. Installation

Hyperlite is **not on PyPI**. Install from a local clone.

### Option A — install script (recommended)

From the repository root:

```powershell
.\scripts\install.ps1
```

This runs CMake (Release), builds `hyperlite.pyd`, and `pip install`s it into your active Python.

Flags:

```powershell
.\scripts\install.ps1 -Python "C:\Path\To\python.exe"
.\scripts\install.ps1 -Editable          # pip install -e . for development
.\scripts\install.ps1 -SkipCudaPath      # don't prepend CUDA bin to PATH
```

### Option B — pip directly

```powershell
pip install .
# or while hacking on the engine:
pip install -e .
```

`setup.py` invokes CMake automatically if `build/Release/hyperlite.pyd` is missing.

### Option C — manual (no pip)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
$env:PYTHONPATH = "C:\path\to\repo\build\Release"
$env:PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1\bin;" + $env:PATH
python python\examples\minimal_game.py
```

### Verify

```powershell
python -c "import hyperlite; e = hyperlite.Engine(64, 64, 'cpu'); print('OK', e.backend_name())"
```

If you develop inside the repo clone, **unset `PYTHONPATH`** before testing the installed package — otherwise Python may import `build/Release/hyperlite.pyd` instead of site-packages:

```powershell
$env:PYTHONPATH = ""
python -c "import hyperlite; print(hyperlite.__file__)"
```

### GPU backend note

If `import hyperlite` fails with “DLL load failed”, ensure:

1. You built with CUDA found by CMake, **and**
2. CUDA’s `bin` folder is on `PATH` (install script adds it automatically), **and**
3. You’re using the same Python bitness (64-bit) as the build.

If no CUDA device is available, pass `backend="cpu"` — the engine falls back gracefully when GPU init fails.

---

## 4. Your first window

```python
import hyperlite

engine = hyperlite.Engine(
    width=800,
    height=600,
    backend="cpu",       # or "gpu"
    title="Hello Hyperlite",
)

while engine.is_running():
    engine.poll_events()
    engine.begin_frame()
    engine.clear(30, 30, 40, 255)
    engine.end_frame()
    engine.present()
```

Close the window with the **X** button, or break the loop from code (see [Input](#7-input)).

---

## 5. The frame loop

Every interactive app follows the same cycle:

```
┌─────────────────────────────────────────┐
│  while engine.is_running():             │
│    poll_events()      ← keyboard/mouse  │
│    update game state  ← your code       │
│    begin_frame()      ← reset command   │
│                         buffer          │
│    draw commands      ← clear, line, …  │
│    end_frame()        ← execute on CPU  │
│                         or GPU          │
│    present()          ← show on screen  │
└─────────────────────────────────────────┘
```

| Step | Who | Purpose |
|------|-----|---------|
| `poll_events()` | Engine | Pump Win32 messages; update key/mouse state |
| `begin_frame()` | Engine | Clear internal command queue for this frame |
| draw calls | You | Queue primitives (not drawn until `end_frame`) |
| `end_frame()` | Engine | Rasterize queued commands to framebuffer |
| `present()` | Engine | Copy framebuffer to window (GPU: device→host readback first) |

**Important:** Drawing calls between `begin_frame` and `end_frame` are **queued**, not executed immediately. Order is preserved (painter’s order).

---

## 6. Drawing

All colors are **RGBA 0–255**. Alpha is stored but blending is not implemented yet — treat it as opaque.

### Clear

Fill the entire framebuffer:

```python
engine.clear(r, g, b, a=255)
```

### Pixel

```python
engine.put_pixel(x, y, r, g, b, a=255)
```

### Line

```python
engine.line(x0, y0, x1, y1, r, g, b, a=255)
```

Clipped to the framebuffer. Uses Bresenham (CPU and GPU match pixel-for-pixel).

### Filled rectangle

```python
engine.rect_fill(x, y, width, height, r, g, b, a=255)
```

`(x, y)` is top-left; width/height in pixels.

### Rectangle outline

```python
engine.rect_outline(x, y, width, height, r, g, b, a=255)
```

### Coordinate system

- Origin **(0, 0)** is **top-left**.
- **X** increases right, **Y** increases down.
- Integer pixel coordinates.

### No built-in circles, text, or images

Implement them yourself (midpoint circle, bitmap font blitting via `put_pixel`, etc.) or batch many `line`/`rect_fill` calls. For heavy procedural scenes, see the GPU spiro helpers in [§8](#8-cpu-vs-gpu-backend).

---

## 7. Input

Always call `poll_events()` once per frame **before** reading input.

### Keyboard

```python
if engine.key_down(0x1B):   # Escape
    break

if engine.key_down(0x57):  # W
    player_y -= speed
```

`key_down(vk_code)` uses **Windows virtual-key codes** (integers 0–255). Common values:

| Key | Code (hex) | Code (dec) |
|-----|------------|------------|
| Escape | `0x1B` | 27 |
| Space | `0x20` | 32 |
| Left | `0x25` | 37 |
| Up | `0x26` | 38 |
| Right | `0x27` | 39 |
| Down | `0x28` | 40 |
| A–Z | `0x41`–`0x5A` | 65–90 |
| 0–9 | `0x30`–`0x39` | 48–57 |

Full list: [Microsoft VK documentation](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).

Keys are **edge-less** for holding: `key_down` is true every frame the key is held.

### Mouse

```python
mx, my = engine.mouse_pos()  # client-area coordinates
```

### Quit

- User closes window → `is_running()` becomes false.
- Or `break` your loop when Escape (or any key) is pressed.

---

## 8. CPU vs GPU backend

```python
engine = hyperlite.Engine(1280, 720, "gpu", "Title")
print(engine.backend_name())  # "gpu" or "cpu" if GPU unavailable
```

### CPU backend (`"cpu"`)

- Draw commands queued in Python → rasterized in C++ on the host.
- Best for: simple scenes, correctness debugging, machines without NVIDIA GPUs.
- Frame loop: standard `begin_frame` → draw → `end_frame` → `present`.

### GPU backend (`"gpu"`)

- Commands/scenes can run on the CUDA device.
- Falls back to CPU if CUDA is missing or init fails.
- `present()` readbacks device memory to the window (small fixed cost per frame).

#### GPU-only helpers

| Method | Description |
|--------|-------------|
| `supports_gpu_scene()` | `True` if device-native scene path is active |
| `clear_gpu(r, g, b, a)` | Clear device framebuffer (no queue) |
| `spiro_scene_cuda(w, h, inst, seg, phase, dt)` | Benchmark scene on GPU |
| `spiro_frame_direct(..., r, g, b, a)` | Fused clear + GPU scene (one call) |
| `tick_gpu_spiro(..., r, g, b, a)` | Poll + fused scene + present (fastest for demos) |
| `set_pipelined(True)` | Overlap readback with next frame (+1 frame latency) |
| `gpu_timings()` | `(upload_ms, kernel_ms, readback_ms)` |

For **typical games** (rects/lines from Python), stay on the normal command path with `backend="gpu"` — the GPU executes your queued `line`/`rect_fill` commands.

Use `set_pipelined(True)` when you’re GPU-bound and want max throughput (see benchmarks).

---

## 9. Building a game — patterns & structure

Hyperlite does not impose architecture. A practical layout for a small game:

```
my_game/
  main.py           # entry point, window + main loop
  game/
    player.py       # Player dataclass + update/draw
    world.py        # tiles, collisions
    input.py        # key constants + read_input(engine)
```

### Minimal game template

See `python/examples/minimal_game.py` — WASD square, grid background, mouse crosshair.

```python
import hyperlite

VK_ESCAPE = 0x1B
VK_W, VK_A, VK_S, VK_D = 0x57, 0x41, 0x53, 0x44

def main():
    engine = hyperlite.Engine(960, 540, "gpu", "My Game")
    px, py = 400, 300
    speed = 4

    while engine.is_running():
        engine.poll_events()
        if engine.key_down(VK_ESCAPE):
            break

        # --- update ---
        if engine.key_down(VK_W): py -= speed
        if engine.key_down(VK_S): py += speed
        if engine.key_down(VK_A): px -= speed
        if engine.key_down(VK_D): px += speed

        # --- draw ---
        engine.begin_frame()
        engine.clear(18, 20, 28, 255)
        engine.rect_fill(px, py, 32, 32, 80, 200, 120, 255)
        engine.end_frame()
        engine.present()

if __name__ == "__main__":
    main()
```

### Recommended patterns

**Game state as plain objects**

```python
from dataclasses import dataclass

@dataclass
class Player:
    x: float
    y: float
    w: int = 32
    h: int = 32

    def update(self, keys, dt):
        ...

    def draw(self, engine):
        engine.rect_fill(int(self.x), int(self.y), self.w, self.h, 200, 100, 50, 255)
```

**Fixed timestep (optional)**

```python
import time

FIXED_DT = 1.0 / 60.0
accum = 0.0
last = time.perf_counter()

while engine.is_running():
    now = time.perf_counter()
    accum += now - last
    last = now
    engine.poll_events()

    while accum >= FIXED_DT:
        world.update(FIXED_DT)
        accum -= FIXED_DT

    engine.begin_frame()
    world.draw(engine)
    engine.end_frame()
    engine.present()
```

**Simple AABB collision**

```python
def rects_overlap(ax, ay, aw, ah, bx, by, bw, bh):
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by
```

**“Sprites” without an engine sprite API**

Store pixels in a `list` or `bytes` (width × height × 4 RGBA), then blit each frame:

```python
def blit_rgba(engine, pixels, sw, sh, dx, dy):
    for y in range(sh):
        for x in range(sw):
            i = (y * sw + x) * 4
            r, g, b, a = pixels[i], pixels[i+1], pixels[i+2], pixels[i+3]
            if a > 128:
                engine.put_pixel(dx + x, dy + y, r, g, b, 255)
```

For performance, prefer `rect_fill`/`line` for large solid regions; `put_pixel` per pixel is fine for small icons or offline baking.

**Layers / draw order**

Queue order = draw order. Draw background first, entities next, UI last:

```python
engine.begin_frame()
draw_background(engine)
draw_entities(engine)
draw_hud(engine)
engine.end_frame()
```

---

## 10. API reference

### Constructor

```python
hyperlite.Engine(width, height, backend="cpu", title="Hyperlite")
```

| Arg | Type | Description |
|-----|------|-------------|
| `width` | int | Framebuffer width (pixels) |
| `height` | int | Framebuffer height |
| `backend` | `"cpu"` \| `"gpu"` | Rendering backend |
| `title` | str | Window title |

### Frame lifecycle

| Method | Returns | Description |
|--------|---------|-------------|
| `poll_events()` | None | Pump OS events |
| `begin_frame()` | None | Start command recording |
| `end_frame()` | None | Execute commands |
| `present()` | None | Show framebuffer |
| `is_running()` | bool | Window open and not quit |

### Drawing (queued)

| Method | Arguments |
|--------|-----------|
| `clear(r, g, b, a=255)` | RGBA |
| `put_pixel(x, y, r, g, b, a=255)` | position + RGBA |
| `line(x0, y0, x1, y1, r, g, b, a=255)` | segment + RGBA |
| `rect_fill(x, y, w, h, r, g, b, a=255)` | rect + RGBA |
| `rect_outline(x, y, w, h, r, g, b, a=255)` | rect + RGBA |

### Input

| Method | Returns |
|--------|---------|
| `key_down(vk_code)` | bool |
| `mouse_pos()` | `(x, y)` tuple |

### Info

| Method | Returns |
|--------|---------|
| `backend_name()` | `"cpu"` or `"gpu"` |

### GPU extras

| Method | Notes |
|--------|-------|
| `supports_gpu_scene()` | bool |
| `clear_gpu(r, g, b, a=255)` | Direct device clear |
| `spiro_scene_cuda(w, h, instances, segments, phase, dt)` | int segment count |
| `spiro_frame_direct(w, h, inst, seg, phase, dt, r, g, b, a=255)` | fused clear+scene |
| `spiro_frame_cuda(...)` | CUDA graph variant (benchmark) |
| `tick_gpu_spiro(w, h, inst, seg, phase, dt, r, g, b, a=255)` | poll+scene+present |
| `set_pipelined(enabled)` | bool/int — enable overlap |
| `gpu_timings()` | `(upload_ms, kernel_ms, readback_ms)` |

### Native spiro helpers (CPU command path)

| Method | Description |
|--------|-------------|
| `spiro_object(...)` | One animated spirograph object |
| `spiro_scene(...)` | Full benchmark scene |
| `spiro_scene_fast(...)` | Same, no return value overhead |

---

## 11. Examples

| File | What it demonstrates |
|------|----------------------|
| `python/examples/minimal_game.py` | WASD mover — **start here for games** |
| `python/examples/pixel_stress.py` | Random pixels + shapes (CPU) |
| `python/examples/uncapped_object_bench.py` | GPU throughput benchmark |
| `python/examples/capped_usage_bench.py` | Fixed FPS + GPU/CPU usage telemetry |

Run after install:

```powershell
python python\examples\minimal_game.py
```

---

## 12. Performance tips

1. **Use `"gpu"` backend** on NVIDIA hardware for heavy drawing.
2. **Batch work in C++** — many small Python calls add overhead; fuse hot paths (see `tick_gpu_spiro` for procedural demos).
3. **`set_pipelined(True)`** when GPU-bound (accept +1 frame display latency).
4. Prefer **`rect_fill` / `line`** over thousands of `put_pixel` calls.
5. Match **window size to framebuffer size** — avoids scaling in the presenter.
6. Read `docs/uncapped-bench-optimization-log.md` for benchmark numbers and ceilings.

---

## 13. Troubleshooting

### `ImportError: DLL load failed while importing hyperlite`

- Rebuild: `pip install . --force-reinstall`
- Add CUDA `bin` to PATH (or use `backend="cpu"`)
- Use 64-bit Python matching the build

### Window opens but stays black

- Ensure you call `end_frame()` before `present()`
- Ensure `clear` or other draws happen **after** `begin_frame()`

### `backend_name()` is `"cpu"` but I asked for `"gpu"`

- CUDA not installed, no NVIDIA GPU, or context init failed — engine falls back silently

### Low FPS with many `put_pixel` calls

- Expected — use rects/lines or move hot loops to native/GPU paths

### Game feels laggy with `set_pipelined(True)`

- Pipelining trades **+1 frame of input latency** for throughput — disable for input-sensitive games:

```python
engine.set_pipelined(False)
```

---

## Next steps for your game

1. Install with `.\scripts\install.ps1`
2. Copy `python/examples/minimal_game.py` as a starting point
3. Add `Player`, `Enemy`, `World` classes that take `engine` in `draw()`
4. Pick `"gpu"` when you have heavy procedural or many primitives; `"cpu"` for simple logic-first prototypes
5. Iterate — Hyperlite stays out of the way; your Python code *is* the game engine layer

For engine internals and optimization history, see `docs/uncapped-bench-optimization-log.md`.
