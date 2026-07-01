"""Capped-FPS efficiency benchmark.

Instead of measuring raw throughput, this benchmark pins the frame rate to a
target (e.g. 60 FPS) and measures how much hardware it costs to sustain that
rate: GPU SM utilization, GPU memory, board power, and host CPU usage. It answers
"how much headroom is left at this workload?" rather than "how fast can it go?".

Pacing is sleep-based (no busy-spin) so the host CPU genuinely idles between
frames -- otherwise a spin-wait pacer would inflate the very CPU number we are
trying to measure.

GPU telemetry uses NVML (nvidia-ml-py); host CPU uses psutil. Both are sampled on
a background thread so polling never stalls the render loop.

Note on GPU utilization: NVML/nvidia-smi 'gpu' utilization counts SM (compute)
activity only. The device->host framebuffer readback runs on the copy engine and
does NOT show up there, so at low complexity SM utilization reads very low even
though the copy engine and PCIe bus are busy every frame.
"""

import argparse
import threading
import time

import hyperlite

try:
	import pynvml

	_HAS_NVML = True
except ImportError:
	_HAS_NVML = False

try:
	import psutil

	_HAS_PSUTIL = True
except ImportError:
	_HAS_PSUTIL = False


def parse_args() -> argparse.Namespace:
	"""Parse runtime options."""
	parser = argparse.ArgumentParser(description="Capped-FPS Hyperlite usage benchmark")
	parser.add_argument("--width", type=int, default=1280, help="Window width in pixels")
	parser.add_argument("--height", type=int, default=720, help="Window height in pixels")
	parser.add_argument("--instances", type=int, default=48, help="Objects per frame")
	parser.add_argument("--segments", type=int, default=96, help="Segments per object")
	parser.add_argument("--backend", type=str, default="gpu", choices=["cpu", "gpu"], help="Rendering backend")
	parser.add_argument("--fps", type=float, default=60.0, help="Target frame rate cap")
	parser.add_argument("--seconds", type=float, default=10.0, help="Benchmark duration in seconds")
	parser.add_argument("--gpu-graph", action="store_true", help="Replay a captured CUDA graph instead of direct launches")
	parser.add_argument("--no-pipeline", action="store_true", help="Disable one-frame-deep present pipelining")
	parser.add_argument("--legacy-python-loop", action="store_true", help="Use multi-call Python frame loop instead of tick_gpu_spiro")
	parser.add_argument("--no-cap", action="store_true", help="Run uncapped but still measure usage")
	parser.add_argument("--sample-ms", type=float, default=100.0, help="Telemetry sample interval in milliseconds")
	return parser.parse_args()


class UsageSampler:
	"""Background sampler for GPU (NVML) and host CPU (psutil) telemetry."""

	def __init__(self, sample_interval_s: float) -> None:
		"""Set up NVML/psutil handles and sampling state."""
		self._interval = sample_interval_s
		self._stop = threading.Event()
		self._thread = threading.Thread(target=self._run, daemon=True)

		# Collected samples.
		self.gpu_util = []  # SM utilization percent
		self.mem_util = []  # memory-controller utilization percent
		self.mem_used_mb = []  # GPU memory in use (MB)
		self.power_w = []  # board power draw (W)
		self.proc_cpu = []  # this process CPU percent (can exceed 100 on multicore)

		self._nvml_handle = None
		if _HAS_NVML:
			pynvml.nvmlInit()
			self._nvml_handle = pynvml.nvmlDeviceGetHandleByIndex(0)
		self._proc = psutil.Process() if _HAS_PSUTIL else None
		if self._proc is not None:
			# Prime cpu_percent so the first real sample is meaningful.
			self._proc.cpu_percent(None)

	def baseline_mem_mb(self) -> float:
		"""Return current GPU memory usage in MB (call before engine creation)."""
		if self._nvml_handle is None:
			return 0.0
		info = pynvml.nvmlDeviceGetMemoryInfo(self._nvml_handle)
		return info.used / (1024.0 * 1024.0)

	def start(self) -> None:
		"""Begin sampling on the background thread."""
		self._thread.start()

	def stop(self) -> None:
		"""Stop sampling and join the thread."""
		self._stop.set()
		self._thread.join(timeout=2.0)
		if self._nvml_handle is not None:
			pynvml.nvmlShutdown()

	def _run(self) -> None:
		"""Sampling loop until stopped."""
		while not self._stop.is_set():
			if self._nvml_handle is not None:
				rates = pynvml.nvmlDeviceGetUtilizationRates(self._nvml_handle)
				self.gpu_util.append(float(rates.gpu))
				self.mem_util.append(float(rates.memory))
				info = pynvml.nvmlDeviceGetMemoryInfo(self._nvml_handle)
				self.mem_used_mb.append(info.used / (1024.0 * 1024.0))
				try:
					self.power_w.append(pynvml.nvmlDeviceGetPowerUsage(self._nvml_handle) / 1000.0)
				except pynvml.NVMLError:
					pass
			if self._proc is not None:
				self.proc_cpu.append(self._proc.cpu_percent(None))
			self._stop.wait(self._interval)


def _avg(values: list) -> float:
	"""Mean of a list, or 0 when empty."""
	return sum(values) / len(values) if values else 0.0


def _peak(values: list) -> float:
	"""Max of a list, or 0 when empty."""
	return max(values) if values else 0.0


def main() -> None:
	"""Run a capped-rate loop and report sustained hardware usage."""
	args = parse_args()

	sampler = UsageSampler(args.sample_ms / 1000.0)
	mem_before_mb = sampler.baseline_mem_mb()

	engine = hyperlite.Engine(args.width, args.height, args.backend, "Hyperlite Capped Usage Bench")
	use_gpu_scene = engine.supports_gpu_scene()
	use_graph = use_gpu_scene and args.gpu_graph
	use_native_tick = use_gpu_scene and not use_graph and not args.legacy_python_loop
	complexity = args.instances * args.segments

	if use_gpu_scene and not args.no_pipeline and not use_graph:
		engine.set_pipelined(True)

	target_dt = 0.0 if args.no_cap else 1.0 / args.fps

	# Per-frame GPU stage timings, averaged across the run.
	kernel_ms_accum = 0.0
	d2h_ms_accum = 0.0
	timing_samples = 0

	frames = 0
	run_start = time.perf_counter()
	next_frame = run_start

	sampler.start()
	while engine.is_running():
		now = time.perf_counter()
		phase = now * 1.9
		dt = target_dt if target_dt > 0.0 else 0.016

		if use_native_tick:
			engine.tick_gpu_spiro(args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255)
		else:
			engine.poll_events()
			engine.begin_frame()
			if use_graph:
				engine.spiro_frame_cuda(args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255)
			elif use_gpu_scene:
				engine.spiro_frame_direct(args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255)
			else:
				engine.clear(10, 10, 16, 255)
				engine.spiro_scene(args.width, args.height, args.instances, args.segments, phase, dt)
			engine.end_frame()
			engine.present()

		frames += 1
		_, kernel_ms, d2h_ms = engine.gpu_timings()
		kernel_ms_accum += kernel_ms
		d2h_ms_accum += d2h_ms
		timing_samples += 1

		elapsed = time.perf_counter() - run_start
		if elapsed >= args.seconds:
			break

		# Sleep-based frame pacing (no busy-spin keeps CPU usage honest).
		if target_dt > 0.0:
			next_frame += target_dt
			sleep_for = next_frame - time.perf_counter()
			if sleep_for > 0.0:
				time.sleep(sleep_for)
			else:
				# Behind schedule: resync to now to avoid a death spiral.
				next_frame = time.perf_counter()

	total_elapsed = time.perf_counter() - run_start
	sampler.stop()

	achieved_fps = frames / total_elapsed if total_elapsed > 0 else 0.0
	avg_kernel_ms = kernel_ms_accum / timing_samples if timing_samples else 0.0
	avg_d2h_ms = d2h_ms_accum / timing_samples if timing_samples else 0.0
	gpu_busy_ms = avg_kernel_ms + avg_d2h_ms
	frame_budget_ms = (1000.0 / args.fps) if not args.no_cap else (1000.0 / achieved_fps if achieved_fps else 0.0)
	headroom_pct = (1.0 - (gpu_busy_ms / frame_budget_ms)) * 100.0 if frame_budget_ms > 0 else 0.0
	mem_avg_mb = _avg(sampler.mem_used_mb)
	mem_delta_mb = mem_avg_mb - mem_before_mb

	cap_label = "uncapped" if args.no_cap else f"{args.fps:.0f}fps cap"
	print(f"=== Capped usage report ({cap_label}) ===")
	print(f"backend={engine.backend_name()} complexity={complexity} (instances={args.instances} segments={args.segments}) duration={total_elapsed:.2f}s")
	print(f"achieved_fps={achieved_fps:.2f} frames={frames}")
	print(f"gpu_sm_util avg={_avg(sampler.gpu_util):.1f}% peak={_peak(sampler.gpu_util):.0f}%   (SM/compute only; excludes D2H copy engine)")
	print(f"gpu_mem_util avg={_avg(sampler.mem_util):.1f}% peak={_peak(sampler.mem_util):.0f}%")
	print(f"gpu_mem_used avg={mem_avg_mb:.0f} MB (engine delta ~{mem_delta_mb:.0f} MB over {mem_before_mb:.0f} MB idle)")
	print(f"gpu_power avg={_avg(sampler.power_w):.1f} W peak={_peak(sampler.power_w):.1f} W")
	print(f"proc_cpu avg={_avg(sampler.proc_cpu):.1f}% (of one core; /{psutil.cpu_count()} cores ~= {_avg(sampler.proc_cpu) / max(psutil.cpu_count(), 1):.1f}% of machine)")
	print(f"gpu_frame_work kernel_ms={avg_kernel_ms:.3f} d2h_ms={avg_d2h_ms:.3f} busy_ms={gpu_busy_ms:.3f} / budget_ms={frame_budget_ms:.3f} -> headroom~{headroom_pct:.1f}%")


if __name__ == "__main__":
	main()
