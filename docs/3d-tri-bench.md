# 3D filled-triangle benchmarks (Layer 1)

Measured on the Hyperlite Linux cloud VM (same class as [linux-bench.md](linux-bench.md) / [3d-wireframe-bench.md](3d-wireframe-bench.md)). Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores |
| ISA | x86_64 with AVX2 (`-march=native`) |
| Compiler | g++ 13.3.0 |
| OpenMP | 4.5 (4 threads) — **enabled over 64×64 tiles** (each tile owns pixels; no depth races) |
| Present | headless |
| CUDA | off |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF -DCMAKE_CXX_COMPILER=g++
cmake --build build -j
```

## Results (this PR)

Workload: **120 frames × 10 000 primitives @ 1280×720**, vsync off, headless. Triangle bench uses world-space tris + perspective camera + depth on + backface cull on (`TickTris3d`).

| Bench | Depth | Path | `total_ms` | Throughput |
|-------|-------|------|------------|------------|
| `cpu_line_bench` (2D int32 screen segs) | n/a | `TickLines` | **128.1** | **9.37e6** lines/s |
| `cpu_line_3d_bench` (world float32 + perspective) | on | `TickLines3d` | **136.5** | **8.79e6** lines/s |
| `cpu_tri_bench` (world float32 filled tris) | on | `TickTris3d` | **447.8** | **2.68e6** tris/s |

Notes:

- Tris fill pixels (half-space coverage + depth), so per-primitive cost is higher than thin lines — expect lower primitives/s than `cpu_line_3d_bench` on the same machine.
- Tile OpenMP recovers parallelism that Layer 0 skipped for depth-on lines.
- Workloads are not pixel-matched; treat the table as paired measurements on one build/machine.
- `enable_depth(False)` leaves 2D + Layer 0 paths unchanged (`reference_render_tests`, `headless_smoke_test`, `depth_wireframe_tests` remain green).

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_line_bench
./build/cpu_line_3d_bench
./build/cpu_tri_bench
```
