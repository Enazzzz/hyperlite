# Hyperlite

Super lightweight immediate-mode renderer for Python — like pygame, but you draw every pixel (or line, or rect) yourself, and the engine stays out of your way.

**Windows & Linux** · **CPU or CUDA GPU backend** · **Not on PyPI** (local install only)

## Quick start

### Windows

**Requirements:** Windows 10/11, Python 3.10+, [Visual Studio 2022](https://visualstudio.microsoft.com/) (C++ workload), [CMake](https://cmake.org/) 3.24+. Optional: [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) for GPU rendering.

```powershell
.\scripts\install.ps1
python python\examples\minimal_game.py
```

### Linux

**Requirements:** g++ (C++20), CMake 3.24+, Python 3.10+ with headers (`python3-dev`). Optional: `libx11-dev` + `libxext-dev` for a real window; CUDA toolkit for `"gpu"`.

```bash
sudo apt-get install -y build-essential g++ cmake python3-dev python3-venv \
  libx11-dev libxext-dev libomp-dev

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
python3 -m venv .venv && .venv/bin/pip install .
# Headless (CI / no DISPLAY):
HYPERLITE_HEADLESS=1 .venv/bin/python -c "import hyperlite; e=hyperlite.Engine(64,64,'cpu',present='headless'); print(e.backend_name())"
# Windowed (needs DISPLAY + X11 libs):
.venv/bin/python python/examples/minimal_game.py
```

Or: `bash scripts/install.sh`

**Full documentation:** [docs/guide.md](docs/guide.md) — installation, API, game loop, GPU path, examples.  
**Native game runtime (optional):** [docs/game-runtime.md](docs/game-runtime.md) — `Game.run()`, jobs, input, world, physics, audio, UI.  
**Linux bench numbers:** [docs/linux-bench.md](docs/linux-bench.md)  
**3D wireframe (Layer 0) + filled tris (Layer 1):** [docs/3d-plan.md](docs/3d-plan.md) · [docs/3d-wireframe-bench.md](docs/3d-wireframe-bench.md) · [docs/3d-tri-bench.md](docs/3d-tri-bench.md)  
**Procedural continent stress game:** [docs/proc-world.md](docs/proc-world.md) — high-detail flyable bench (`python/examples/proc_world.py`).

## What you get

```python
import hyperlite

engine = hyperlite.Engine(1280, 720, "cpu", "My Game")  # or backend="gpu"

while engine.is_running():
    engine.poll_events()
    engine.begin_frame()
    engine.clear(20, 20, 30, 255)
    engine.rect_fill(100, 100, 64, 64, 255, 80, 80, 255)
    engine.end_frame()
    engine.present()
```

No scene graph, no sprites, no UI toolkit — just a fast framebuffer, draw commands, input, and a window (or headless present for tests/CI).

### Present modes

| Mode | How | When |
|------|-----|------|
| `auto` (default) | Window if a display exists, else headless | Interactive apps |
| `headless` | No window; `present()` is a no-op | CI, benches, servers |
| `window` | Win32 (Windows) or X11 (Linux) | Force a window |

Override with `present="headless"` / env `HYPERLITE_HEADLESS=1` / `HYPERLITE_PRESENT=headless|window`.

## Project layout

| Path | Purpose |
|------|---------|
| `engine/` | C++ core (Win32/X11/headless, CPU/GPU rasterizers) |
| `bindings/python/` | Python C extension |
| `python/examples/` | Demos and benchmarks |
| `docs/guide.md` | **How-to guide** |
| `docs/linux-bench.md` | Linux baseline numbers |
| `docs/3d-plan.md` | 3D layer roadmap (wireframe → tris → mesh → GPU) |
| `scripts/install.ps1` / `install.sh` | One-command local install |

## License

MIT (see repository; add `LICENSE` file if you publish a fork).
