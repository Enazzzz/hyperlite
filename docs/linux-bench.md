# Linux baseline benchmarks

Measured on the Hyperlite Linux port cloud VM. Numbers are real wall-clock results from Release builds — not estimates.

## Machine

| Item | Value |
|------|-------|
| OS | Ubuntu 24.04 (linux 6.12.x) |
| CPU | Intel Xeon (KVM), 4 cores @ ~4.8 BogoMIPS/thread |
| ISA | x86_64 with AVX2 (Release used `-march=native`) |
| RAM | 15 GiB |
| Compiler | g++ 13.3.0 |
| CMake | 3.28.3 |
| OpenMP | 4.5 (`-fopenmp`), 4 threads |
| CUDA | not installed (CPU-only) |
| Present | headless (`PresentMode::kHeadless` / `HYPERLITE_HEADLESS=1`) |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ \
  -DHYPERLITE_ENABLE_CUDA=OFF
cmake --build build -j
```

Release flags: `-O3 -march=native -ffast-math` + OpenMP.

## Tests

| Suite | Result |
|-------|--------|
| `reference_render_tests` | **PASS** |
| `headless_smoke_test` | **PASS** |
| `depth_wireframe_tests` | **PASS** (Layer 0 3D) |
| `ctest` (all) | **100% passed** |

### Headless smoke (`./build/headless_smoke_test`)

| Metric | Value |
|--------|-------|
| `Engine(1280,720,"cpu")` startup | **1.17 ms** |
| RSS after init | **8164 KiB** |
| RSS after 100 `TickLines` frames | **8460 KiB** |
| Framebuffer FNV-1a hash | `5329638197634890789` |

## CPU benchmarks

### `cpu_line_bench` (120 frames × 10 000 lines @ 1280×720)

| Metric | Value |
|--------|-------|
| `total_ms` | **130.803** |
| `lines_per_second` | **9.17e6** (~9.17 million lines/s) |

Approximate pixel fill rate is workload-dependent (short wireframe segments); line throughput is the primary metric for this bench.

### `primitive_bench` (120 frames × 50 000 put-pixel draws)

| Metric | Value |
|--------|-------|
| `total_ms` | **52.837** |
| `draws_per_second` | **1.14e8** (~114 million draws/s) |

### GPU benches

`gpu_scene_bench` / `gpu_blit_bench` skipped: CUDA toolkit not present on this VM.

## Python smoke

```text
pip install .   # into a venv → hyperlite.cpython-312-*.so
Engine(1280, 720, "cpu", present="headless")
clear / rect_fill / line / end_frame / present → ok
startup_ms ≈ 1.25
```

## Reproduce

```bash
export HYPERLITE_HEADLESS=1
./build/reference_render_tests
./build/headless_smoke_test
./build/depth_wireframe_tests
./build/cpu_line_bench
./build/cpu_line_3d_bench
./build/primitive_bench
ctest --test-dir build --output-on-failure
```

3D wireframe numbers: [3d-wireframe-bench.md](3d-wireframe-bench.md).
