"""Uncapped benchmark that draws animated arbitrary objects.



Defaults to the GPU-first path: ``tick_gpu_spiro`` runs poll + fused clear/scene +

present in one native call. Pass ``--backend cpu`` to compare against the reference

software rasterizer.

"""



import argparse

import time



import hyperlite





def parse_args() -> argparse.Namespace:

	"""Parse runtime options for benchmark scaling."""

	parser = argparse.ArgumentParser(description="Uncapped Hyperlite object benchmark")

	parser.add_argument("--width", type=int, default=1280, help="Window width in pixels")

	parser.add_argument("--height", type=int, default=720, help="Window height in pixels")

	parser.add_argument("--instances", type=int, default=48, help="How many objects to draw per frame")

	parser.add_argument("--segments", type=int, default=96, help="Segments per object")

	parser.add_argument("--backend", type=str, default="gpu", choices=["cpu", "gpu"], help="Rendering backend")

	parser.add_argument("--seconds", type=float, default=0.0, help="Auto-exit benchmark duration in seconds (0 means infinite)")

	parser.add_argument("--gpu-graph", action="store_true", help="Replay a captured CUDA graph instead of direct GPU launches")

	parser.add_argument("--no-pipeline", action="store_true", help="Disable one-frame-deep present pipelining")

	parser.add_argument("--legacy-python-loop", action="store_true", help="Use multi-call Python frame loop instead of tick_gpu_spiro")

	return parser.parse_args()





def main() -> None:

	"""Run an uncapped frame loop and print benchmark telemetry."""

	args = parse_args()

	engine = hyperlite.Engine(args.width, args.height, args.backend, "Hyperlite Uncapped Object Bench")



	use_gpu_scene = engine.supports_gpu_scene()

	use_graph = use_gpu_scene and args.gpu_graph

	use_native_tick = use_gpu_scene and not use_graph and not args.legacy_python_loop

	complexity = args.instances * args.segments



	# Pipelined present is on by default for GPU (CUDA graphs use a fixed buffer).
	if use_gpu_scene and not args.no_pipeline and not use_graph:
		engine.set_pipelined(True)



	run_start = time.perf_counter()

	last_report = time.perf_counter()

	last_phase = last_report

	frame_count = 0

	accum_draw_calls = 0

	total_frames = 0

	total_draw_calls = 0



	# No sleep anywhere: this keeps the benchmark fully uncapped.

	while engine.is_running():

		now = time.perf_counter()

		dt = now - last_phase

		last_phase = now

		phase = now * 1.9



		if use_native_tick:

			frame_draw_calls = int(

				engine.tick_gpu_spiro(

					args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255

				)

			)

		else:

			engine.poll_events()

			engine.begin_frame()

			if use_graph:

				frame_draw_calls = int(

					engine.spiro_frame_cuda(

						args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255

					)

				)

			elif use_gpu_scene:

				frame_draw_calls = int(

					engine.spiro_frame_direct(

						args.width, args.height, args.instances, args.segments, phase, dt, 10, 10, 16, 255

					)

				)

			else:

				engine.clear(10, 10, 16, 255)

				frame_draw_calls = int(engine.spiro_scene(args.width, args.height, args.instances, args.segments, phase, dt))

			engine.end_frame()

			engine.present()



		frame_count += 1

		accum_draw_calls += frame_draw_calls

		total_frames += 1

		total_draw_calls += frame_draw_calls



		report_now = time.perf_counter()

		if report_now - last_report >= 1.0:

			elapsed = report_now - last_report

			fps = frame_count / elapsed

			draws_per_sec = accum_draw_calls / elapsed

			frame_ms = (elapsed / frame_count) * 1000.0 if frame_count else 0.0

			upload_ms, kernel_ms, readback_ms = engine.gpu_timings()

			print(

				f"fps={fps:.2f} frame_ms={frame_ms:.3f} draws/s={draws_per_sec:,.0f} "

				f"complexity={complexity} "

				f"upload_ms={upload_ms:.3f} kernel_ms={kernel_ms:.3f} d2h_ms={readback_ms:.3f} "

				f"backend={engine.backend_name()}"

			)

			last_report = report_now

			frame_count = 0

			accum_draw_calls = 0



		if args.seconds > 0.0 and (report_now - run_start) >= args.seconds:

			total_elapsed = report_now - run_start

			avg_fps = total_frames / total_elapsed if total_elapsed > 0 else 0.0

			avg_draws = total_draw_calls / total_elapsed if total_elapsed > 0 else 0.0

			upload_ms, kernel_ms, readback_ms = engine.gpu_timings()

			print(

				f"summary avg_fps={avg_fps:.2f} avg_draws/s={avg_draws:,.0f} "

				f"complexity={complexity} duration={total_elapsed:.2f}s "

				f"upload_ms={upload_ms:.3f} kernel_ms={kernel_ms:.3f} d2h_ms={readback_ms:.3f} "

				f"instances={args.instances} segments={args.segments} backend={engine.backend_name()}"

			)

			break





if __name__ == "__main__":

	main()


