# Uncapped Object Bench Optimization Log

Configuration for all runs unless noted:

- command: `python/examples/uncapped_object_bench.py --seconds 6 --instances 48 --segments 96 --backend cpu`
- renderer: windowed, uncapped
- interpreter: `Python311`

| Iteration | Change | avg_fps | avg_draws/s | Delta FPS |
| --- | --- | ---: | ---: | ---: |
| 0 | Baseline Python-heavy object generation | 359.66 | 1,657,321 | -- |
| 1 | Native `spiro_object` generation in C extension | 1767.21 | 8,143,291 | +1407.55 |
| 2 | Native full-scene generation `spiro_scene` | 1874.86 | 8,639,346 | +107.65 |
| 3 | Trig recurrence in native spiro generation | 2072.60 | 9,550,563 | +197.74 |
| 4 | Raster line inner-loop/index fast path | 2229.71 | 10,274,525 | +157.11 |
| 5 | Remove `lround` in native scene generation | 2241.89 | 10,330,637 | +12.18 |
| 6 | Additional trig-step precompute pass | 2145.57 | 9,886,785 | -96.32 |
| 7 | Final validation run (current state) | 2161.66 | 9,960,934 | +16.09 vs iter 6 |

Notes:

- Iterations 6-7 show noise and no stable gain over iteration 5 best-case.
- Meaningful gains appear exhausted; further low-level changes currently fluctuate under thermal/scheduling variance rather than clear improvements.

## CUDA-First Phase (GPU as Execution Core)

The CPU rasterizer hit a ceiling around 2.2k FPS at the smallest scene
(`48x96`, complexity 4608). The engine was then made GPU-first: scene geometry
generation and rasterization run in CUDA kernels, the device owns the
framebuffer, and the CPU only stages the present copy. The primary metric shifts
from raw FPS to **scene complexity per frame** (instances x segments) and a
**frame time breakdown** (kernel vs device->host readback).

Hardware: NVIDIA GPU, compute capability `sm_75`, CUDA Toolkit 13.1.

### Windowed comparison (Python loop + Win32 present)

`python/examples/uncapped_object_bench.py --seconds 4`, `1280x720`.

| Complexity | CPU FPS | GPU FPS | CPU draws/s | GPU draws/s |
| ---: | ---: | ---: | ---: | ---: |
| 4,608 | 1538 | 1078 | 7.1M | 5.0M |
| 65,536 | 604 | 698 | 39.6M | 45.7M |
| 131,072 | 341 | 646 | 44.7M | 84.7M |
| 262,144 | 195 | 600 | 51.1M | 157M |
| 524,288 | 110 | 409 | 57.6M | 214M |

The crossover is near complexity 65k. Below it, fixed per-frame overhead
(kernel launch, stream sync, D2H readback, `StretchDIBits` present, Python loop)
dominates and the CPU wins. Above it the GPU scales far better: at complexity
524,288 the GPU sustains 409 FPS vs the CPU's 110 FPS (3.7x), and at equal frame
time the GPU carries roughly an order of magnitude more scene complexity.

### Headless GPU sweep (`benchmarks/gpu_scene_bench.cpp`)

No window or Python in the loop; isolates pure device throughput and the
readback floor. `1280x720`, 300 timed frames per point.

| Complexity | avg frame ms | FPS | draws/s | kernel ms | D2H ms |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,608 | 0.442 | 2260 | 10.4M | 0.059 | 0.319 |
| 65,536 | 0.588 | 1701 | 112M | 0.193 | 0.318 |
| 131,072 | 0.761 | 1315 | 172M | 0.345 | 0.351 |
| 262,144 | 1.071 | 933 | 245M | 0.634 | 0.346 |
| 524,288 | 1.685 | 593 | 311M | 1.228 | 0.384 |
| 1,048,576 | 2.878 | 347 | 364M | 2.401 | 0.384 |
| 2,097,152 | 5.352 | 187 | 392M | 4.788 | 0.438 |

Observations:

- Peak GPU raster throughput is ~392M draws/s versus the CPU ceiling of ~58M
  draws/s, roughly a 6.8x improvement in pure rasterization work.
- At low complexity the frame floor is the device->host readback (~0.32 ms for a
  1280x720 RGBA8 frame, ~11.5 GB/s over pinned PCIe), while the kernel is only
  ~0.06 ms. Without GPU-direct presentation (which would require a graphics API),
  this readback is the hard lower bound for the windowed path.

### GPU optimizations applied

| Change | Effect |
| --- | --- |
| Replace `TryCudaRender` stub with real CUDA raster kernels + persistent context | GPU executes clear/pixel/line/rect; CPU-GPU pixel parity verified |
| Device-resident framebuffer, allocated once and resized only on size change | zero device allocations in the hot loop |
| Fused spiro scene kernel (one thread per segment, closed-form trig) | generation + rasterization in a single launch |
| Pin (page-lock) the present target and DMA the readback directly into it | low-complexity windowed GPU 840 -> 1078 FPS (+28%) by removing a host staging copy |
| CUDA Graph capture of the clear+generate sequence (opt-in `--gpu-graph`) | implemented and correct, but ~4% slower for this 2-kernel frame; direct launches remain the default (graphs amortize only over many nodes) |

### Utilization & efficiency (capped-FPS view)

`python/examples/capped_usage_bench.py` pins the frame rate and measures hardware
cost instead of throughput. GPU telemetry via NVML (`nvidia-ml-py`), host CPU via
`psutil`. Pacing is sleep-based so the CPU genuinely idles between frames.

Important: NVML "gpu" utilization counts SM/compute activity only. The
device->host framebuffer readback runs on the **copy engine** and is invisible
there, so at low complexity SM utilization reads very low even though the PCIe
bus is busy every frame.

| Scenario | complexity | achieved FPS | GPU SM util | proc CPU (1 core) | GPU mem delta | power | headroom |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| GPU 60 FPS cap | 4,608 | 60.1 | 10.3% | 12.0% | ~96 MB | 23.4 W | 96.1% |
| GPU 60 FPS cap | 262,144 | 60.1 | 12.6% | 14.2% | ~82 MB | 25.1 W | 92.2% |
| GPU uncapped | 262,144 | 562 | 81.6% | 92.5% | ~66 MB | 34.6 W | 29.3% |
| CPU 60 FPS cap | 262,144 | 60.1 | 8.6% | 32.6% | ~21 MB | 26.0 W | 100% |

Why the GPU never reaches ~100% util (the "63% cap" symptom):

- The render loop synchronizes once per frame, so the SMs idle during the
  single-threaded CPU work that follows: `StretchDIBits` present, `poll_events`,
  the Python loop, and the readback wait. That idle gap is the bubble.
- Uncapped at 262k, the render thread is already at ~92% of one core while the
  GPU draws only **34.6 W** on a board rated far higher -- the workload is
  CPU-serialization-bound and idle-bound, not power/thermal/compute-bound.
- At equal 60 FPS, the GPU backend costs ~14% of a core vs the CPU backend's
  ~33% (~2.3x more CPU-efficient) and leaves >92% GPU headroom.

The remaining path to higher GPU utilization is to pipeline frames (present frame
N-1 while computing frame N via double-buffered device/host staging), trading one
frame of latency to hide the CPU present/poll/readback bubble.

### Frame pipelining + host-thread optimizations (2026-06-30)

Follow-up pass to push throughput further:

- **`cudaDeviceScheduleBlockingSync`**: host thread blocks during CUDA sync instead
  of spin-waiting at 100% CPU (frees a core for other work; lower proc_cpu in
  capped benchmarks).
- **`SpiroSceneFrameDirect`**: fused clear + scene in one native call (no CUDA
  graph capture overhead).
- **`tick_gpu_spiro`**: poll + fused GPU frame + present in **one** Python call
  (eliminates ~5 Python crossings per frame).
- **Double device framebuffers + dedicated copy stream**: compute on stream A,
  D2H on stream B so frame N+1 kernels overlap frame N readback (fixes the
  inflated `kernel_ms` from single-buffer serialization).
- **`SetDIBitsToDevice`**: fast present path when window size matches framebuffer
  (no scaling).

Pipelining is **on by default** for GPU benchmarks (`--no-pipeline` to disable).
CUDA graphs (`--gpu-graph`) disable pipelining because graphs target a fixed buffer.

Measured windowed uncapped (1280x720, RTX 4060 Laptop, `tick_gpu_spiro` + pipeline):

| complexity | before (single-buffer) | after (full pass) | Δ |
| ---: | ---: | ---: | ---: |
| 4,608 | 1063 | 1064 | ~0% |
| 262,144 | 564 | **654** | **+16%** |
| 1,048,576 | 214 | **281** | **+31%** |

At 262k complexity GPU SM util reaches ~85% with ~0% frame headroom (fully
GPU-bound). Low complexity remains readback/present-floor limited (~0.93 ms/frame).

### Stop condition

Per the plan, the optimization loop stops when no stable gain >= 1 FPS or >= 1 MB
appears at fixed complexity across repeated runs. The remaining windowed
low-complexity overhead is dominated by the readback floor and OS present, which
cannot be reduced further without a GPU-direct present path (out of scope: no
external rendering libraries). The GPU path meets its goal of scaling by scene
complexity well beyond the CPU ceiling.
