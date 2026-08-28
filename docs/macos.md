# Hyperlite on macOS

Hyperlite is a **CPU software renderer**. On macOS it still owns every pixel: Cocoa is only a present/input surface. There is no Metal or OpenGL raster path.

## What is supported

| Area | macOS behavior |
|------|----------------|
| Window / present | `NSWindow` + layer-backed `NSView`. Host RGBA8 is converted to BGRA and assigned to `CALayer.contents` (nearest-neighbor, stretched — including Retina). |
| Input | `NSEvent` polled into the same Win32 VK map used on Windows and X11 (`Keys.Escape`, WASD, arrows, F11, …). |
| Mouse capture | Cursor hidden; `CGAssociateMouseAndMouseCursorPosition(false)` + warp to center. |
| Fullscreen | `NSWindow toggleFullScreen:` |
| Vsync | `CVDisplayLink` wait on `Present` when enabled |
| Gamepad | `GameController.framework` (Xbox-style button/axis layout) |
| Audio output | Core Audio `AudioQueue` pulls `AudioSystem::Mix()` (stereo int16, 44.1 kHz). `Game.run()` starts this automatically. |
| CUDA / DXGI | Not available. `backend="gpu"` is CPU-only on Mac. |
| SIMD | Apple Silicon uses the existing scalar raster paths (no AVX2). Intel Macs can still get AVX2 via `-march=native`. |
| OpenMP | Apple Clang usually has none. Line batches stay serial unless you install Homebrew `libomp`. |

Headless present (`HYPERLITE_HEADLESS=1` / `present="headless"`) works the same as Linux CI.

## Requirements

| Component | Required | Notes |
|-----------|----------|-------|
| macOS 11+ | Yes | Apple Silicon and Intel |
| Xcode Command Line Tools | Yes | `xcode-select --install` |
| CMake 3.24+ | Yes | `brew install cmake` |
| Python 3.10+ | Yes | Headers come with python.org / Homebrew python |
| Homebrew `libomp` | Optional | OpenMP for CPU line batches |
| CUDA | No | Not used on macOS |

## Build

```bash
xcode-select --install   # once
brew install cmake python

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
HYPERLITE_HEADLESS=1 ctest --test-dir build --output-on-failure

python3 -m venv .venv
.venv/bin/pip install .
HYPERLITE_HEADLESS=1 .venv/bin/python -c \
  "import hyperlite; e=hyperlite.Engine(64,64,'cpu',present='headless'); print(e.backend_name())"
```

Or: `bash scripts/install.sh`

Windowed:

```bash
.venv/bin/python python/examples/minimal_game.py
.venv/bin/python python/examples/native_game.py
```

## Environment

| Variable | Effect |
|----------|--------|
| `HYPERLITE_HEADLESS=1` | Force headless present (no `NSWindow`) |
| `HYPERLITE_PRESENT=window` | Force Cocoa even if a session looks headless |
| `HYPERLITE_PRESENT=headless` | Same as `HYPERLITE_HEADLESS=1` |

`DisplayAvailable()` is true on macOS (Aqua). Unset `DISPLAY` does **not** force headless here — that variable is an X11/Wayland signal. Use `HYPERLITE_HEADLESS` in CI.

## Architecture notes

- AppKit must run on the **main thread**. Drive `Engine` / `Game` from the thread that created the window (typical Python main).
- Framebuffer size is engine pixels. A Retina display stretches the software framebuffer; raster resolution does not follow `backingScaleFactor`.
- `NativeHandle()` is `NSWindow*`.
- `AudioSystem.StartOutput()` is a no-op on Linux/Windows; Mix-to-buffer still works everywhere.
- Portable `-DHYPERLITE_MARCH=x86-64` is ignored on Apple Silicon.

## Troubleshooting

- **No window in SSH:** Cocoa creation throws and the factory falls back to headless. Use a logged-in GUI session or `HYPERLITE_HEADLESS=1`.
- **No sound:** `Game.run()` calls `StartOutput()`. A lone `Engine` loop does not; call `game.start_audio_output()` or mix into your own backend.
- **Slow lines:** install `libomp` or live with the scalar OpenMP fallback — raster correctness is unchanged.
- **Import error after pip:** reinstall with the same interpreter (`pip install . --force-reinstall`).
