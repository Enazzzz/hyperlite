# Hyperlite

Super lightweight immediate-mode renderer for Python — like pygame, but you draw every pixel (or line, or rect) yourself, and the engine stays out of your way.

**Windows only** · **CPU or CUDA GPU backend** · **Not on PyPI** (local install only)

## Quick start

**Requirements:** Windows 10/11, Python 3.10+, [Visual Studio 2022](https://visualstudio.microsoft.com/) (C++ workload), [CMake](https://cmake.org/) 3.24+. Optional: [CUDA Toolkit](https://developer.nvidia.com/cuda-downloads) for GPU rendering.

```powershell
# From the repo root — builds native code + installs into your active Python
.\scripts\install.ps1

# Run a demo
python python\examples\minimal_game.py
python python\examples\pixel_stress.py
```

Manual install:

```powershell
pip install .
# or editable while developing:
pip install -e .
```

**Full documentation:** [docs/guide.md](docs/guide.md) — installation, API reference, game loop patterns, GPU path, examples, troubleshooting.

## What you get

```python
import hyperlite

engine = hyperlite.Engine(1280, 720, "gpu", "My Game")

while engine.is_running():
    engine.poll_events()
    engine.begin_frame()
    engine.clear(20, 20, 30, 255)
    engine.rect_fill(100, 100, 64, 64, 255, 80, 80, 255)
    engine.end_frame()
    engine.present()
```

No scene graph, no sprites, no UI toolkit — just a fast framebuffer, draw commands, input, and a window.

## Project layout

| Path | Purpose |
|------|---------|
| `engine/` | C++ core (Win32 window, CPU/GPU rasterizers) |
| `bindings/python/` | Python C extension |
| `python/examples/` | Demos and benchmarks |
| `docs/guide.md` | **How-to guide (start here for games)** |
| `scripts/install.ps1` | One-command local install |

## License

MIT (see repository; add `LICENSE` file if you publish a fork).
