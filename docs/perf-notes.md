# Hyperlite Performance Notes

## Design for Throughput
- Explicit command buffering avoids implicit allocations in draw paths.
- Framebuffer storage is contiguous `RGBA8` for predictable cache behavior.
- CPU backend is the correctness baseline and fallback path.

## Optimization Checklist
- Measure first: frame time (`p50`, `p95`), draw calls/sec, and memory footprint.
- Keep per-frame heap allocations at zero in hot paths.
- Prioritize branch reduction and contiguous memory writes in raster loops.
- Validate GPU path correctness against CPU reference output every change.

## Next Optimization Steps
- Add SIMD-specialized clear/fill/blit loops (`SSE2`/`AVX2`).
- Implement CUDA kernels for clear, point, and rectangle primitives.
- Add backend profiler counters exposed to Python for runtime telemetry.
