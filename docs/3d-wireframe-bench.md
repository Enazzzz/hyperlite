# 3D wireframe benchmarks (Layer 0)

Measured on the Hyperlite Linux cloud VM (same class as [linux-bench.md](linux-bench.md)). Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores |
| ISA | x86_64 with AVX2 (`-march=native`) |
| Compiler | g++ 13.3.0 |
| OpenMP | 4.5 (4 threads) — **disabled for depth-on 3D** (serial segments to avoid depth races) |
| Present | headless |
| CUDA | off |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
```

## Results (this PR)

Workload: **120 frames × 10 000 lines @ 1280×720**, vsync off, headless.

| Bench | Depth | Path | `total_ms` | `lines_per_second` |
|-------|-------|------|------------|--------------------|
| `cpu_line_bench` (2D int32 screen segs) | n/a | `TickLines` | **143.5** | **8.36e6** |
| `cpu_line_3d_bench` (world float32 + perspective) | on | `TickLines3d` | **133.6** | **8.98e6** |

Notes:

- Prior Linux-port baseline for 2D was ~**9.17e6** lines/s ([linux-bench.md](linux-bench.md)); this run’s 2D number is in the same ballpark (VM noise).
- 3D includes view-proj multiply, homogeneous clip, perspective divide, and float32 depth test/write per pixel. Segment lengths after projection differ from the 2D grid workload, so treat the table as paired measurements on the same machine/build, not a pure apples-to-apples microbench of “depth tax only.”
- `enable_depth(False)` leaves the existing 2D path unchanged (`reference_render_tests` + `headless_smoke_test` remain green).

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
ctest --test-dir build --output-on-failure
./build/cpu_line_bench
./build/cpu_line_3d_bench
```
