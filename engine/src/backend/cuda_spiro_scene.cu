#include "cuda_spiro_scene.cuh"

#include "cuda_raster_device.cuh"

namespace hyperlite::cuda_detail {

namespace {

constexpr double kTau = 6.283185307179586476925286766559;

/**
 * Pack RGBA channels into the engine's 32-bit layout (R in the low byte).
 *
 * Must match hyperlite::PackColor so GPU output is consistent with the CPU
 * path and the Win32 presenter.
 */
__device__ inline std::uint32_t DevicePackColor(const int r, const int g, const int b, const int a) {
	return static_cast<std::uint32_t>(r & 0xFF) |
		(static_cast<std::uint32_t>(g & 0xFF) << 8U) |
		(static_cast<std::uint32_t>(b & 0xFF) << 16U) |
		(static_cast<std::uint32_t>(a & 0xFF) << 24U);
}

/**
 * Evaluate one spirograph vertex using the same closed-form angles as the CPU
 * incremental recurrence in QueueSpiroObjectNative.
 */
__device__ inline void EvalSpiroPoint(
	const int vertex,
	const double local_phase,
	const double step_t,
	const int center_x,
	const int center_y,
	const double radius,
	int& out_x,
	int& out_y) {
	const double a1 = local_phase * 0.7 + static_cast<double>(vertex) * 2.3 * step_t;
	const double a2 = -local_phase * 1.1 + static_cast<double>(vertex) * 3.7 * step_t;
	const double a3 = local_phase + static_cast<double>(vertex) * 7.0 * step_t;
	const double rr = radius + sin(a3) * (radius * 0.28);
	out_x = static_cast<int>(static_cast<double>(center_x) + cos(a1) * rr);
	out_y = static_cast<int>(static_cast<double>(center_y) + sin(a2) * rr);
}

/**
 * Per-thread work shared by the scalar and device-parameter kernels: one thread
 * owns one line segment, computes both endpoints, and rasterizes the line.
 */
__device__ inline void SpiroSegment(
	std::uint32_t* fb,
	const int width,
	const int height,
	const int instances,
	const int segments,
	const int grid_cols,
	const double cell_w,
	const double cell_h,
	const double phase,
	const double dt,
	const double step_t) {
	const long long g = static_cast<long long>(blockIdx.x) * blockDim.x + threadIdx.x;
	const long long total = static_cast<long long>(instances) * static_cast<long long>(segments);
	if (g >= total) {
		return;
	}

	const int idx = static_cast<int>(g / segments);
	const int s = static_cast<int>(g % segments);

	const int col = idx % grid_cols;
	const int row = idx / grid_cols;
	const int center_x = static_cast<int>((static_cast<double>(col) + 0.5) * cell_w);
	const int center_y = static_cast<int>((static_cast<double>(row) + 0.5) * cell_h);
	const double radius = fmin(cell_w, cell_h) * (0.30 + 0.08 * sin(phase + static_cast<double>(idx) * 0.17));

	const double hue_shift = static_cast<double>(idx) * 0.11 + phase * 0.25;
	const int r = static_cast<int>(127.0 + 127.0 * sin(hue_shift + 0.0));
	const int gg = static_cast<int>(127.0 + 127.0 * sin(hue_shift + 2.09));
	const int b = static_cast<int>(127.0 + 127.0 * sin(hue_shift + 4.18));
	const std::uint32_t packed = DevicePackColor(r, gg, b, 255);

	const double local_phase = phase + static_cast<double>(idx) * 0.23 + dt * 10.0;

	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	EvalSpiroPoint(s, local_phase, step_t, center_x, center_y, radius, x0, y0);
	EvalSpiroPoint(s + 1, local_phase, step_t, center_x, center_y, radius, x1, y1);

	DeviceDrawLine(fb, x0, y0, x1, y1, width, height, packed);
}

/**
 * Fused generation + rasterization kernel with scalar animation params.
 */
__global__ void SpiroSceneKernel(
	std::uint32_t* fb,
	const int width,
	const int height,
	const int instances,
	const int segments,
	const int grid_cols,
	const double cell_w,
	const double cell_h,
	const double phase,
	const double dt,
	const double step_t) {
	SpiroSegment(fb, width, height, instances, segments, grid_cols, cell_w, cell_h, phase, dt, step_t);
}

/**
 * Same kernel but reads animation params from device memory so a captured CUDA
 * graph can be replayed with fresh phase/dt without re-capturing.
 */
__global__ void SpiroSceneKernelDev(
	std::uint32_t* fb,
	const int width,
	const int height,
	const int instances,
	const int segments,
	const int grid_cols,
	const double cell_w,
	const double cell_h,
	const double* params,
	const double step_t) {
	SpiroSegment(fb, width, height, instances, segments, grid_cols, cell_w, cell_h, params[0], params[1], step_t);
}

/**
 * Shared host helper: derive grid layout and launch dimensions.
 */
struct SpiroLaunchPlan {
	bool valid;
	int grid_cols;
	double cell_w;
	double cell_h;
	double step_t;
	int blocks;
	int total;
};

SpiroLaunchPlan PlanScene(const int width, const int height, const int instances, const int segments) {
	SpiroLaunchPlan plan{};
	plan.valid = !(width <= 0 || height <= 0 || instances <= 0 || segments <= 1);
	if (!plan.valid) {
		return plan;
	}
	int grid_cols = static_cast<int>(sqrt(static_cast<double>(instances)));
	if (grid_cols < 1) {
		grid_cols = 1;
	}
	const int grid_rows = (instances + grid_cols - 1) / grid_cols;
	plan.grid_cols = grid_cols;
	plan.cell_w = static_cast<double>(width) / static_cast<double>(grid_cols);
	plan.cell_h = static_cast<double>(height) / static_cast<double>(grid_rows);
	plan.step_t = kTau / static_cast<double>(segments);
	const long long total = static_cast<long long>(instances) * static_cast<long long>(segments);
	constexpr int block = 256;
	plan.blocks = static_cast<int>((total + block - 1) / block);
	plan.total = instances * segments;
	return plan;
}

constexpr int kSpiroBlock = 256;

} // namespace

int LaunchSpiroScene(
	std::uint32_t* d_framebuffer,
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double phase,
	const double dt,
	cudaStream_t stream) {
	if (d_framebuffer == nullptr) {
		return 0;
	}
	const SpiroLaunchPlan plan = PlanScene(width, height, instances, segments);
	if (!plan.valid) {
		return 0;
	}

	SpiroSceneKernel<<<plan.blocks, kSpiroBlock, 0, stream>>>(
		d_framebuffer,
		width,
		height,
		instances,
		segments,
		plan.grid_cols,
		plan.cell_w,
		plan.cell_h,
		phase,
		dt,
		plan.step_t);

	return plan.total;
}

int LaunchSpiroSceneDev(
	std::uint32_t* d_framebuffer,
	const int width,
	const int height,
	const int instances,
	const int segments,
	const double* d_params,
	cudaStream_t stream) {
	if (d_framebuffer == nullptr || d_params == nullptr) {
		return 0;
	}
	const SpiroLaunchPlan plan = PlanScene(width, height, instances, segments);
	if (!plan.valid) {
		return 0;
	}

	SpiroSceneKernelDev<<<plan.blocks, kSpiroBlock, 0, stream>>>(
		d_framebuffer,
		width,
		height,
		instances,
		segments,
		plan.grid_cols,
		plan.cell_w,
		plan.cell_h,
		d_params,
		plan.step_t);

	return plan.total;
}

} // namespace hyperlite::cuda_detail
