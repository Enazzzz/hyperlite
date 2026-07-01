#include <Python.h>

#include <algorithm>
#include <exception>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/engine.hpp"

using hyperlite::BackendKind;
using hyperlite::Color;
using hyperlite::DrawCommand;
using hyperlite::Engine;

/**
 * Python object that wraps one native engine instance.
 */
typedef struct {
	PyObject_HEAD
	Engine* native_engine;
} PyEngineObject;

/**
 * Convert backend string into native backend enum.
 */
static BackendKind ParseBackendKind(const char* backend_name) {
	if (backend_name && std::strcmp(backend_name, "gpu") == 0) {
		return BackendKind::kGpu;
	}
	return BackendKind::kCpu;
}

/**
 * Build DrawCommand helper with consistent color packing.
 */
static DrawCommand MakeCommand(
	const hyperlite::CommandType type,
	const int x0,
	const int y0,
	const int x1,
	const int y1,
	const Color color) {
	DrawCommand command{};
	command.type = type;
	command.x0 = x0;
	command.y0 = y0;
	command.x1 = x1;
	command.y1 = y1;
	command.packed_color = hyperlite::PackColor(color);
	return command;
}

/**
 * Precomputed angular increments reused across one benchmark frame.
 */
struct SpiroStepConstants {
	double step_sin1;
	double step_cos1;
	double step_sin2;
	double step_cos2;
	double step_sin3;
	double step_cos3;
};

/**
 * Queue one spirograph object using incremental trig recurrence.
 */
static int QueueSpiroObjectNative(
	Engine* engine,
	const int center_x,
	const int center_y,
	const double radius,
	const double phase,
	const int segments,
	const std::uint32_t packed,
	const SpiroStepConstants& step_constants) {
	if (segments <= 1 || radius <= 0.0) {
		return 0;
	}

	double sin1 = std::sin(phase * 0.7);
	double cos1 = std::cos(phase * 0.7);
	double sin2 = std::sin(-phase * 1.1);
	double cos2 = std::cos(-phase * 1.1);
	double sin3 = std::sin(phase);
	double cos3 = std::cos(phase);

	int prev_x = 0;
	int prev_y = 0;
	int draw_calls = 0;
	for (int i = 0; i <= segments; ++i) {
		const double wobble = sin3 * (radius * 0.28);
		const double rr = radius + wobble;
		const int x = static_cast<int>(static_cast<double>(center_x) + cos1 * rr);
		const int y = static_cast<int>(static_cast<double>(center_y) + sin2 * rr);

		if (i > 0) {
			engine->PushCommand({hyperlite::CommandType::kLine, prev_x, prev_y, x, y, packed});
			++draw_calls;
		}
		prev_x = x;
		prev_y = y;

		const double next_sin1 = (sin1 * step_constants.step_cos1) + (cos1 * step_constants.step_sin1);
		const double next_cos1 = (cos1 * step_constants.step_cos1) - (sin1 * step_constants.step_sin1);
		sin1 = next_sin1;
		cos1 = next_cos1;

		const double next_sin2 = (sin2 * step_constants.step_cos2) + (cos2 * step_constants.step_sin2);
		const double next_cos2 = (cos2 * step_constants.step_cos2) - (sin2 * step_constants.step_sin2);
		sin2 = next_sin2;
		cos2 = next_cos2;

		const double next_sin3 = (sin3 * step_constants.step_cos3) + (cos3 * step_constants.step_sin3);
		const double next_cos3 = (cos3 * step_constants.step_cos3) - (sin3 * step_constants.step_sin3);
		sin3 = next_sin3;
		cos3 = next_cos3;
	}

	return draw_calls;
}

/**
 * Allocate Python object memory for Engine wrapper.
 */
static PyObject* PyEngine_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
	(void)args;
	(void)kwargs;
	auto* self = reinterpret_cast<PyEngineObject*>(type->tp_alloc(type, 0));
	if (!self) {
		return nullptr;
	}
	self->native_engine = nullptr;
	return reinterpret_cast<PyObject*>(self);
}

/**
 * Initialize native engine from Python constructor args.
 */
static int PyEngine_init(PyEngineObject* self, PyObject* args, PyObject* kwargs) {
	static const char* kwlist[] = {"width", "height", "backend", "title", nullptr};
	int width = 0;
	int height = 0;
	const char* backend = "cpu";
	const char* title = "Hyperlite";

	if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|ss", const_cast<char**>(kwlist), &width, &height, &backend, &title)) {
		return -1;
	}

	try {
		self->native_engine = new Engine(width, height, ParseBackendKind(backend), title);
	} catch (const std::exception& ex) {
		PyErr_SetString(PyExc_RuntimeError, ex.what());
		return -1;
	}
	return 0;
}

/**
 * Destroy native engine resources when object is collected.
 */
static void PyEngine_dealloc(PyEngineObject* self) {
	delete self->native_engine;
	self->native_engine = nullptr;
	Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

/**
 * Begin frame command recording.
 */
static PyObject* PyEngine_begin_frame(PyEngineObject* self, PyObject* args) {
	(void)args;
	self->native_engine->BeginFrame();
	Py_RETURN_NONE;
}

/**
 * Execute queued commands into framebuffer.
 */
static PyObject* PyEngine_end_frame(PyEngineObject* self, PyObject* args) {
	(void)args;
	self->native_engine->EndFrame();
	Py_RETURN_NONE;
}

/**
 * Present rendered frame to platform window.
 */
static PyObject* PyEngine_present(PyEngineObject* self, PyObject* args) {
	(void)args;
	self->native_engine->Present();
	Py_RETURN_NONE;
}

/**
 * Pump OS events and refresh input state.
 */
static PyObject* PyEngine_poll_events(PyEngineObject* self, PyObject* args) {
	(void)args;
	self->native_engine->PollEvents();
	Py_RETURN_NONE;
}

/**
 * Return engine running state.
 */
static PyObject* PyEngine_is_running(PyEngineObject* self, PyObject* args) {
	(void)args;
	if (self->native_engine->IsRunning()) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Return active backend identifier.
 */
static PyObject* PyEngine_backend_name(PyEngineObject* self, PyObject* args) {
	(void)args;
	const auto name = self->native_engine->BackendName();
	return PyUnicode_FromStringAndSize(name.data(), static_cast<Py_ssize_t>(name.size()));
}

/**
 * Queue clear command.
 */
static PyObject* PyEngine_clear(PyEngineObject* self, PyObject* args) {
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iii|i", &r, &g, &b, &a)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeCommand(hyperlite::CommandType::kClear, 0, 0, 0, 0, color));
	Py_RETURN_NONE;
}

/**
 * Queue single-pixel command.
 */
static PyObject* PyEngine_put_pixel(PyEngineObject* self, PyObject* args) {
	int x = 0;
	int y = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiii|i", &x, &y, &r, &g, &b, &a)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeCommand(hyperlite::CommandType::kPutPixel, x, y, 0, 0, color));
	Py_RETURN_NONE;
}

/**
 * Queue line command.
 */
static PyObject* PyEngine_line(PyEngineObject* self, PyObject* args) {
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiiii|i", &x0, &y0, &x1, &y1, &r, &g, &b, &a)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeCommand(hyperlite::CommandType::kLine, x0, y0, x1, y1, color));
	Py_RETURN_NONE;
}

/**
 * Queue filled rectangle command.
 */
static PyObject* PyEngine_rect_fill(PyEngineObject* self, PyObject* args) {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiiii|i", &x, &y, &w, &h, &r, &g, &b, &a)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeCommand(hyperlite::CommandType::kRectFill, x, y, w, h, color));
	Py_RETURN_NONE;
}

/**
 * Queue outline rectangle command.
 */
static PyObject* PyEngine_rect_outline(PyEngineObject* self, PyObject* args) {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiiii|i", &x, &y, &w, &h, &r, &g, &b, &a)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeCommand(hyperlite::CommandType::kRectOutline, x, y, w, h, color));
	Py_RETURN_NONE;
}

/**
 * Query key-down state by virtual-key code.
 */
static PyObject* PyEngine_key_down(PyEngineObject* self, PyObject* args) {
	int key_code = 0;
	if (!PyArg_ParseTuple(args, "i", &key_code)) {
		return nullptr;
	}
	if (key_code < 0 || key_code > 255) {
		PyErr_SetString(PyExc_ValueError, "key_code must be in [0, 255].");
		return nullptr;
	}
	if (self->native_engine->GetInputState().key_down[static_cast<std::size_t>(key_code)]) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Return current mouse coordinates in client space.
 */
static PyObject* PyEngine_mouse_pos(PyEngineObject* self, PyObject* args) {
	(void)args;
	const auto& pos = self->native_engine->GetInputState().mouse_pos;
	return Py_BuildValue("(ii)", pos.x, pos.y);
}

/**
 * Queue one spirograph-like object entirely in native code.
 */
static PyObject* PyEngine_spiro_object(PyEngineObject* self, PyObject* args) {
	int center_x = 0;
	int center_y = 0;
	double radius = 0.0;
	double phase = 0.0;
	int segments = 0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiddiiii|i", &center_x, &center_y, &radius, &phase, &segments, &r, &g, &b, &a)) {
		return nullptr;
	}

	if (segments <= 1 || radius <= 0.0) {
		return PyLong_FromLong(0);
	}

	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const std::uint32_t packed = hyperlite::PackColor(color);
	const double tau = 6.283185307179586476925286766559;
	const double step_t = tau / static_cast<double>(segments);
	const SpiroStepConstants step_constants{
		std::sin(2.3 * step_t),
		std::cos(2.3 * step_t),
		std::sin(3.7 * step_t),
		std::cos(3.7 * step_t),
		std::sin(7.0 * step_t),
		std::cos(7.0 * step_t)};
	const int draw_calls = QueueSpiroObjectNative(self->native_engine, center_x, center_y, radius, phase, segments, packed, step_constants);
	return PyLong_FromLong(draw_calls);
}

/**
 * Queue an entire arbitrary-object benchmark scene natively.
 */
static PyObject* PyEngine_spiro_scene(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	if (!PyArg_ParseTuple(args, "iiiidd", &width, &height, &instances, &segments, &phase, &dt)) {
		return nullptr;
	}

	if (width <= 0 || height <= 0 || instances <= 0 || segments <= 1) {
		return PyLong_FromLong(0);
	}

	int grid_cols = static_cast<int>(std::sqrt(static_cast<double>(instances)));
	if (grid_cols < 1) {
		grid_cols = 1;
	}
	const int grid_rows = (instances + grid_cols - 1) / grid_cols;
	const double cell_w = static_cast<double>(width) / static_cast<double>(grid_cols);
	const double cell_h = static_cast<double>(height) / static_cast<double>(grid_rows);
	const double tau = 6.283185307179586476925286766559;
	const double step_t = tau / static_cast<double>(segments);
	const SpiroStepConstants step_constants{
		std::sin(2.3 * step_t),
		std::cos(2.3 * step_t),
		std::sin(3.7 * step_t),
		std::cos(3.7 * step_t),
		std::sin(7.0 * step_t),
		std::cos(7.0 * step_t)};

	int total_draw_calls = 0;

	for (int idx = 0; idx < instances; ++idx) {
		const int col = idx % grid_cols;
		const int row = idx / grid_cols;
		const int center_x = static_cast<int>((static_cast<double>(col) + 0.5) * cell_w);
		const int center_y = static_cast<int>((static_cast<double>(row) + 0.5) * cell_h);
		const double radius = std::min(cell_w, cell_h) * (0.30 + 0.08 * std::sin(phase + static_cast<double>(idx) * 0.17));

		const double hue_shift = static_cast<double>(idx) * 0.11 + phase * 0.25;
		const int r = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 0.0));
		const int g = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 2.09));
		const int b = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 4.18));
		const Color color{
			static_cast<std::uint8_t>(r),
			static_cast<std::uint8_t>(g),
			static_cast<std::uint8_t>(b),
			255U};
		const std::uint32_t packed = hyperlite::PackColor(color);

		const double local_phase = phase + static_cast<double>(idx) * 0.23 + dt * 10.0;
		total_draw_calls += QueueSpiroObjectNative(self->native_engine, center_x, center_y, radius, local_phase, segments, packed, step_constants);
	}

	return PyLong_FromLong(total_draw_calls);
}

/**
 * Queue full benchmark scene without returning per-frame allocation.
 */
static PyObject* PyEngine_spiro_scene_fast(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	if (!PyArg_ParseTuple(args, "iiiidd", &width, &height, &instances, &segments, &phase, &dt)) {
		return nullptr;
	}
	if (width <= 0 || height <= 0 || instances <= 0 || segments <= 1) {
		Py_RETURN_NONE;
	}

	int grid_cols = static_cast<int>(std::sqrt(static_cast<double>(instances)));
	if (grid_cols < 1) {
		grid_cols = 1;
	}
	const int grid_rows = (instances + grid_cols - 1) / grid_cols;
	const double cell_w = static_cast<double>(width) / static_cast<double>(grid_cols);
	const double cell_h = static_cast<double>(height) / static_cast<double>(grid_rows);
	const double tau = 6.283185307179586476925286766559;
	const double step_t = tau / static_cast<double>(segments);
	const SpiroStepConstants step_constants{
		std::sin(2.3 * step_t),
		std::cos(2.3 * step_t),
		std::sin(3.7 * step_t),
		std::cos(3.7 * step_t),
		std::sin(7.0 * step_t),
		std::cos(7.0 * step_t)};

	for (int idx = 0; idx < instances; ++idx) {
		const int col = idx % grid_cols;
		const int row = idx / grid_cols;
		const int center_x = static_cast<int>((static_cast<double>(col) + 0.5) * cell_w);
		const int center_y = static_cast<int>((static_cast<double>(row) + 0.5) * cell_h);
		const double radius = std::min(cell_w, cell_h) * (0.30 + 0.08 * std::sin(phase + static_cast<double>(idx) * 0.17));

		const double hue_shift = static_cast<double>(idx) * 0.11 + phase * 0.25;
		const int r = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 0.0));
		const int g = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 2.09));
		const int b = static_cast<int>(127.0 + 127.0 * std::sin(hue_shift + 4.18));
		const Color color{
			static_cast<std::uint8_t>(r),
			static_cast<std::uint8_t>(g),
			static_cast<std::uint8_t>(b),
			255U};
		const std::uint32_t packed = hyperlite::PackColor(color);

		const double local_phase = phase + static_cast<double>(idx) * 0.23 + dt * 10.0;
		(void)QueueSpiroObjectNative(self->native_engine, center_x, center_y, radius, local_phase, segments, packed, step_constants);
	}

	Py_RETURN_NONE;
}

/**
 * Report whether the active backend can run scenes fully on the GPU.
 */
static PyObject* PyEngine_supports_gpu_scene(PyEngineObject* self, PyObject* args) {
	(void)args;
	if (self->native_engine->SupportsGpuScene()) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Clear the device framebuffer directly on the GPU (no command recording).
 */
static PyObject* PyEngine_clear_gpu(PyEngineObject* self, PyObject* args) {
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iii|i", &r, &g, &b, &a)) {
		return nullptr;
	}
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	self->native_engine->ClearGpu(hyperlite::PackColor(color));
	Py_RETURN_NONE;
}

/**
 * Generate and rasterize the spiro benchmark scene entirely on the GPU.
 */
static PyObject* PyEngine_spiro_scene_cuda(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	if (!PyArg_ParseTuple(args, "iiiidd", &width, &height, &instances, &segments, &phase, &dt)) {
		return nullptr;
	}
	const int draw_calls = self->native_engine->SpiroSceneGpu(width, height, instances, segments, phase, dt);
	return PyLong_FromLong(draw_calls);
}

/**
 * Clear + generate + rasterize the spiro scene through a captured CUDA graph.
 */
static PyObject* PyEngine_spiro_frame_cuda(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiddiii|i", &width, &height, &instances, &segments, &phase, &dt, &r, &g, &b, &a)) {
		return nullptr;
	}
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_calls = self->native_engine->SpiroSceneFrameGpu(
		width, height, instances, segments, phase, dt, hyperlite::PackColor(color));
	return PyLong_FromLong(draw_calls);
}

/**
 * Clear + generate + rasterize the spiro scene via direct GPU kernel launches.
 */
static PyObject* PyEngine_spiro_frame_direct(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiddiii|i", &width, &height, &instances, &segments, &phase, &dt, &r, &g, &b, &a)) {
		return nullptr;
	}
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_calls = self->native_engine->SpiroSceneFrameDirectGpu(
		width, height, instances, segments, phase, dt, hyperlite::PackColor(color));
	return PyLong_FromLong(draw_calls);
}

/**
 * Native GPU frame tick: poll events, fused clear+scene, present in one call.
 */
static PyObject* PyEngine_tick_gpu_spiro(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	int instances = 0;
	int segments = 0;
	double phase = 0.0;
	double dt = 0.0;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "iiiiddiii|i", &width, &height, &instances, &segments, &phase, &dt, &r, &g, &b, &a)) {
		return nullptr;
	}
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_calls = self->native_engine->TickGpuSpiro(
		width, height, instances, segments, phase, dt, hyperlite::PackColor(color));
	return PyLong_FromLong(draw_calls);
}

/**
 * Enable/disable one-frame-deep present pipelining (GPU backend only).
 */
static PyObject* PyEngine_set_pipelined(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetPipelined(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Return last-frame GPU stage timings as (upload_ms, kernel_ms, readback_ms).
 */
static PyObject* PyEngine_gpu_timings(PyEngineObject* self, PyObject* args) {
	(void)args;
	const hyperlite::GpuTimings timings = self->native_engine->GpuTimingsLast();
	return Py_BuildValue(
		"(ddd)",
		static_cast<double>(timings.upload_ms),
		static_cast<double>(timings.kernel_ms),
		static_cast<double>(timings.readback_ms));
}

static PyMethodDef PyEngine_methods[] = {
	{"begin_frame", reinterpret_cast<PyCFunction>(PyEngine_begin_frame), METH_NOARGS, "Begin frame command recording."},
	{"end_frame", reinterpret_cast<PyCFunction>(PyEngine_end_frame), METH_NOARGS, "Execute queued commands."},
	{"present", reinterpret_cast<PyCFunction>(PyEngine_present), METH_NOARGS, "Present current framebuffer."},
	{"poll_events", reinterpret_cast<PyCFunction>(PyEngine_poll_events), METH_NOARGS, "Pump platform events."},
	{"is_running", reinterpret_cast<PyCFunction>(PyEngine_is_running), METH_NOARGS, "Return whether the window is alive."},
	{"backend_name", reinterpret_cast<PyCFunction>(PyEngine_backend_name), METH_NOARGS, "Return active backend name."},
	{"clear", reinterpret_cast<PyCFunction>(PyEngine_clear), METH_VARARGS, "Queue clear command."},
	{"put_pixel", reinterpret_cast<PyCFunction>(PyEngine_put_pixel), METH_VARARGS, "Queue pixel command."},
	{"line", reinterpret_cast<PyCFunction>(PyEngine_line), METH_VARARGS, "Queue line command."},
	{"rect_fill", reinterpret_cast<PyCFunction>(PyEngine_rect_fill), METH_VARARGS, "Queue filled rectangle command."},
	{"rect_outline", reinterpret_cast<PyCFunction>(PyEngine_rect_outline), METH_VARARGS, "Queue outline rectangle command."},
	{"key_down", reinterpret_cast<PyCFunction>(PyEngine_key_down), METH_VARARGS, "Read key state by virtual key code."},
	{"mouse_pos", reinterpret_cast<PyCFunction>(PyEngine_mouse_pos), METH_NOARGS, "Return mouse position."},
	{"spiro_object", reinterpret_cast<PyCFunction>(PyEngine_spiro_object), METH_VARARGS, "Queue one native spirograph object and return line count."},
	{"spiro_scene", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene), METH_VARARGS, "Queue full native spirograph benchmark scene and return line count."},
	{"spiro_scene_fast", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene_fast), METH_VARARGS, "Queue full native spirograph scene without return allocation."},
	{"supports_gpu_scene", reinterpret_cast<PyCFunction>(PyEngine_supports_gpu_scene), METH_NOARGS, "Return whether the backend runs scenes on the GPU."},
	{"clear_gpu", reinterpret_cast<PyCFunction>(PyEngine_clear_gpu), METH_VARARGS, "Clear the device framebuffer directly on the GPU."},
	{"spiro_scene_cuda", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene_cuda), METH_VARARGS, "Generate and rasterize the spiro scene fully on the GPU."},
	{"spiro_frame_cuda", reinterpret_cast<PyCFunction>(PyEngine_spiro_frame_cuda), METH_VARARGS, "Clear + scene via captured CUDA graph; returns segment count."},
	{"spiro_frame_direct", reinterpret_cast<PyCFunction>(PyEngine_spiro_frame_direct), METH_VARARGS, "Clear + scene via direct GPU launches; returns segment count."},
	{"tick_gpu_spiro", reinterpret_cast<PyCFunction>(PyEngine_tick_gpu_spiro), METH_VARARGS, "Poll + fused clear/scene + present in one native call."},
	{"gpu_timings", reinterpret_cast<PyCFunction>(PyEngine_gpu_timings), METH_NOARGS, "Return last-frame (upload_ms, kernel_ms, readback_ms)."},
	{"set_pipelined", reinterpret_cast<PyCFunction>(PyEngine_set_pipelined), METH_VARARGS, "Enable/disable one-frame-deep present pipelining (GPU only)."},
	{nullptr, nullptr, 0, nullptr},
};

static PyTypeObject PyEngineType = {
	PyVarObject_HEAD_INIT(nullptr, 0)
};

static PyModuleDef HyperliteModule = {
	PyModuleDef_HEAD_INIT,
	"hyperlite",
	"Super lightweight explicit rendering engine module.",
	-1,
	nullptr,
	nullptr,
	nullptr,
	nullptr,
	nullptr
};

/**
 * Create module and register Engine type.
 */
PyMODINIT_FUNC PyInit_hyperlite() {
	PyEngineType.tp_name = "hyperlite.Engine";
	PyEngineType.tp_basicsize = sizeof(PyEngineObject);
	PyEngineType.tp_flags = Py_TPFLAGS_DEFAULT;
	PyEngineType.tp_doc = "Hyperlite rendering engine wrapper.";
	PyEngineType.tp_new = PyEngine_new;
	PyEngineType.tp_init = reinterpret_cast<initproc>(PyEngine_init);
	PyEngineType.tp_dealloc = reinterpret_cast<destructor>(PyEngine_dealloc);
	PyEngineType.tp_methods = PyEngine_methods;

	if (PyType_Ready(&PyEngineType) < 0) {
		return nullptr;
	}

	PyObject* module = PyModule_Create(&HyperliteModule);
	if (!module) {
		return nullptr;
	}

	Py_INCREF(&PyEngineType);
	if (PyModule_AddObject(module, "Engine", reinterpret_cast<PyObject*>(&PyEngineType)) < 0) {
		Py_DECREF(&PyEngineType);
		Py_DECREF(module);
		return nullptr;
	}
	return module;
}
