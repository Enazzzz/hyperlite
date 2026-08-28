# Supported platforms

Hyperlite is a **CPU software renderer**. It owns every pixel. OS graphics APIs (GDI, DXGI, X11, Cocoa) are **present/input only** — they never take over rasterization. There is no Vulkan, OpenGL, Metal, or D3D raster path.

## Matrix

| | Windows 10/11 | Linux (X11) | macOS 11+ |
|--|---------------|-------------|-----------|
| CPU raster (`backend="cpu"`) | Yes | Yes | Yes (scalar on Apple Silicon; AVX2 on Intel with `-march=native`) |
| CUDA raster (`backend="gpu"`) | Optional toolkit | Optional toolkit | No |
| Windowed present | Win32 + DXGI (GDI fallback) | X11 + MIT-SHM | Cocoa layer blit of host RGBA8 |
| Headless present | Yes | Yes | Yes |
| Keyboard / mouse | Win32 | X11 | NSEvent (same VK map: Escape, Tab, WASD, arrows, Space, F11, Return) |
| Gamepad (`InputRuntime`) | Stub (`connected=false`) | `/dev/input/js*` | GameController.framework |
| Audio device | Mix-to-buffer only | Mix-to-buffer only | Core Audio `AudioQueue` (started by `Game.run()`) |
| OpenMP line batches | MSVC `/openmp` | `libomp-dev` | Optional Homebrew `libomp` (Apple Clang usually has none) |
| CI | `.github/workflows/windows-ci.yml` | `linux-ci.yml` | `macos-ci.yml` |

Python **3.10+**. CMake **3.24+**. C++20.

## Requirements by OS

### Windows

- Visual Studio 2022+ with the C++ workload (CI uses the newest VS on `windows-latest`)
- CMake on `PATH`
- Optional: CUDA Toolkit for `"gpu"`

```powershell
.\scripts\install.ps1
python python\examples\minimal_game.py
python python\examples\native_game.py
```

### Linux

- `g++` (C++20), `python3-dev`, CMake
- Optional: `libx11-dev` `libxext-dev` (window), `libomp-dev` (OpenMP), CUDA toolkit

```bash
sudo apt-get install -y build-essential g++ cmake python3-dev python3-venv \
  libx11-dev libxext-dev libomp-dev
bash scripts/install.sh
HYPERLITE_HEADLESS=1 .venv/bin/python python/examples/native_game.py
```

Without X11 libs, the engine still builds and runs **headless**.

### macOS

See [macos.md](macos.md). Summary: Xcode Command Line Tools, CMake, Python 3.10+. CUDA is not used. `HYPERLITE_MARCH=x86-64` is ignored on Apple Silicon.

```bash
xcode-select --install
brew install cmake python
bash scripts/install.sh
```

## Environment

| Variable | Effect |
|----------|--------|
| `HYPERLITE_HEADLESS=1` | Force headless present |
| `HYPERLITE_PRESENT=headless\|window` | Override auto present |
| `HYPERLITE_MARCH` | CMake `-march` (default `native`; `x86-64` for portable Linux) |
| `HYPERLITE_ENABLE_CUDA=1` | Force CUDA probe on macOS (normally off) |
| `DISPLAY` / `WAYLAND_DISPLAY` | Linux: unset → auto headless. **Ignored on macOS.** |

## What Python can do vs C++

The public Python module exports **`Engine`**, **`Game`**, **`Keys`**, **`MouseButtons`**.

- **`Engine`** is the complete immediate-mode renderer used by existing games (draw, input, meshes, present).
- **`Game`** is a thinner Python façade over the native runtime: `run()` / `step()`, input edges, optional `on_frame`, borrowed `engine()`, entity handles, instance draw, profiler. Physics, world, audio Mix, UI, nav, and jobs stay **C++-only** (`engine/include/engine/runtime/`).

That split is intentional: Python is not required on the hot path. See [game-runtime.md](game-runtime.md).

## v1 limits (honest)

This is **alpha 0.1.0**, local install, not a Unity/Godot substitute.

- Raster is CPU software (optional CUDA compute on Windows/Linux). Apple Silicon is scalar.
- Windows `InputRuntime` gamepad: stub (`connected=false`).
- Linux/Windows: software Mix only — no OS audio device. macOS: Core Audio via `Game.run()`.
- Python does not bind physics, world, UI, nav, jobs, or the CPU shader VM.
- Triangle/mesh fill tiles are **128×128**; dirty present tiles are **64px**.
