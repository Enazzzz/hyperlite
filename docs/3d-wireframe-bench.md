# 3D wireframe benchmarks (Layer 0)

Measured on the Hyperlite Linux cloud VM (same class as [linux-bench.md](linux-bench.md) / [cloud-agent-vm-specs.md](cloud-agent-vm-specs.md)). Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores |
| ISA | x86_64 with AVX2 + FMA (`-march=native`) |
| Compiler | g++ 13.3.0 |
| OpenMP | 4.5 (4 threads) — depth-off: OpenMP over segments; depth-on: hybrid serial vs **64px-tall full-width row strips** (exclusive pixel ownership; no depth races) |
| Present | headless |
| CUDA | off |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
```

## What shipped (this PR)

In `cpu_line_raster_3d.hpp` / `cpu_line_raster.hpp` (public API unchanged):

1. **Project-once scratch** — depth-on batches transform/clip/project into reused `Line3dDrawScratch` (same process-static pattern as mesh bins; not `thread_local`).
2. **64px row-strip OpenMP** — bin by Y into dirty-tile-height strips spanning the full width; each strip rasters only the clipped segment parts that cover it (existing H/V span + pointer Bresenham fill unchanged).
3. **Hybrid gate** — parallel only when `segs >= 384` and mean Chebyshev screen span `max(|dx|,|dy|) >= 48`; short-diagonal batches stay serial (bin/fork tax otherwise wins).
4. **`ClipLineToRect`** — shared Cohen–Sutherland helper for framebuffer and strip scissors.
5. **Bench** — `cpu_line_3d_bench` default = short diagonals; `cpu_line_3d_bench long` = multi-row spans.

## Paired benches (this VM)

Release, headless, `HYPERLITE_ENABLE_CUDA=OFF`, `HYPERLITE_MARCH=native`, `OMP_NUM_THREADS=4`. Before = current `main` (serial depth-on); after = this PR. Same machine; headers swapped and rebuilt for the before leg so the long workload is comparable.

Workload: **120 frames × 10 000 lines @ 1280×720**, vsync off, headless. Medians over 6 runs (3D) / 4 runs (2D).

| Bench | Before (lines/s) | After (lines/s) | Δ |
|-------|------------------|-----------------|---|
| `cpu_line_3d_bench` short (depth on) | **11.40e6** | **11.75e6** | **~flat / slight +** (hybrid → serial) |
| `cpu_line_3d_bench long` (depth on) | **2.35e6** | **4.46e6** | **~+90%** (row-strip OpenMP) |
| `cpu_line_bench` (2D int32) | **~8.1e6** (noisy) | **~8.6e6** | **~flat (noise)** |

Projected mean span on this camera/workload: short ≈ **14 px** (mostly one 64×64 tile); long ≈ **216 px** (~3.2 row strips, ~14× 64×64 tiles).

Portable `HYPERLITE_MARCH=x86-64`: `ctest` green (including `depth_wireframe_tests`).

## Experiments

| Experiment | Result |
|------------|--------|
| Always-on **64×64** AABB bins + OpenMP over tiles | **Loss** on short (~−40%) and long (~−25%) vs serial — too many clip restarts per segment |
| Always-on **64px-tall × full-width** row strips | **Loss** on short; **win** on long |
| Hybrid (serial if mean span &lt; 48, else row strips) | **Shipped** — preserves short bench; ~+90% on long |
| Thread-count horizontal bands (4 strips) | Also wins on long; row strips preferred (same 64px height as dirty tiles / tri bins) |
| OpenMP over segments with depth on | **Not shipped** (depth races without per-region ownership) |

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
export OMP_NUM_THREADS=4
ctest --test-dir build --output-on-failure
./build/cpu_line_bench
./build/cpu_line_3d_bench
./build/cpu_line_3d_bench long
cmake -S . -B build-portable -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DHYPERLITE_MARCH=x86-64
ctest --test-dir build-portable --output-on-failure
```
