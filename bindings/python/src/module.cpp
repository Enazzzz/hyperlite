#include <Python.h>

#include <algorithm>
#include <exception>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "engine/engine.hpp"
#include "engine/sprite_draw.hpp"

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
 * Build a line draw command with optional stroke width.
 */
static DrawCommand MakeLineCommand(
	const int x0,
	const int y0,
	const int x1,
	const int y1,
	const Color color,
	const int width) {
	DrawCommand command = MakeCommand(hyperlite::CommandType::kLine, x0, y0, x1, y1, color);
	const int clamped = std::max(1, std::min(width, 255));
	command.line_width = static_cast<std::uint8_t>(clamped);
	return command;
}

/**
 * Convert an object to a vector of int coordinates.
 *
 * Accepts contiguous 32-bit integer buffers or generic Python sequences.
 */
static bool ParseIntSequence(PyObject* obj, std::vector<int>& out, const char* arg_name) {
	Py_buffer view{};
	if (PyObject_CheckBuffer(obj) != 0 && PyObject_GetBuffer(obj, &view, PyBUF_FORMAT | PyBUF_CONTIG_RO) == 0) {
		const bool format_ok = (view.format == nullptr) ||
			(std::strcmp(view.format, "i") == 0) ||
			(std::strcmp(view.format, "I") == 0) ||
			(std::strcmp(view.format, "l") == 0) ||
			(std::strcmp(view.format, "L") == 0);
		if (view.itemsize == static_cast<Py_ssize_t>(sizeof(int)) && format_ok) {
			const int* ptr = reinterpret_cast<const int*>(view.buf);
			const std::size_t count = static_cast<std::size_t>(view.len / view.itemsize);
			out.assign(ptr, ptr + count);
			PyBuffer_Release(&view);
			return true;
		}
		PyBuffer_Release(&view);
		PyErr_Format(PyExc_TypeError, "%s buffer must use 32-bit integer elements.", arg_name);
		return false;
	}

	PyObject* fast = PySequence_Fast(obj, nullptr);
	if (fast == nullptr) {
		PyErr_Format(PyExc_TypeError, "%s must be a sequence or int32 buffer.", arg_name);
		return false;
	}
	const Py_ssize_t n = PySequence_Fast_GET_SIZE(fast);
	if (n < 0) {
		Py_DECREF(fast);
		PyErr_Format(PyExc_ValueError, "%s has invalid length.", arg_name);
		return false;
	}
	out.reserve(static_cast<std::size_t>(n));
	PyObject** items = PySequence_Fast_ITEMS(fast);
	for (Py_ssize_t i = 0; i < n; ++i) {
		const long v = PyLong_AsLong(items[i]);
		if (PyErr_Occurred()) {
			Py_DECREF(fast);
			PyErr_Format(PyExc_TypeError, "%s contains a non-integer value.", arg_name);
			return false;
		}
		out.push_back(static_cast<int>(v));
	}
	Py_DECREF(fast);
	return true;
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
			engine->PushCommand(hyperlite::MakeDrawCommand(hyperlite::CommandType::kLine, prev_x, prev_y, x, y, packed));
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
 * Poll events, end frame, and present in one native call.
 */
static PyObject* PyEngine_tick(PyEngineObject* self, PyObject* args) {
	(void)args;
	self->native_engine->Tick();
	Py_RETURN_NONE;
}

/**
 * Upload a full RGBA8 frame as this frame's base image.
 */
static PyObject* PyEngine_upload_frame_rgba(PyEngineObject* self, PyObject* args) {
	PyObject* src_obj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &src_obj)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(src_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "buffer must expose a contiguous readonly byte view.");
		return nullptr;
	}
	self->native_engine->UploadFrameRgba(reinterpret_cast<const std::uint8_t*>(view.buf), static_cast<std::size_t>(view.len));
	PyBuffer_Release(&view);
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
 * Queue many pixel commands from x/y vectors in one Python crossing.
 */
static PyObject* PyEngine_put_pixels(PyEngineObject* self, PyObject* args) {
	PyObject* xs_obj = nullptr;
	PyObject* ys_obj = nullptr;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "OOiii|i", &xs_obj, &ys_obj, &r, &g, &b, &a)) {
		return nullptr;
	}

	std::vector<int> xs{};
	std::vector<int> ys{};
	if (!ParseIntSequence(xs_obj, xs, "xs") || !ParseIntSequence(ys_obj, ys, "ys")) {
		return nullptr;
	}
	if (xs.size() != ys.size()) {
		PyErr_SetString(PyExc_ValueError, "xs and ys must have the same length.");
		return nullptr;
	}

	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const std::uint32_t packed = hyperlite::PackColor(color);
	std::vector<hyperlite::DrawCommand> batch(xs.size());
	for (std::size_t i = 0; i < xs.size(); ++i) {
		batch[i] = hyperlite::MakeDrawCommand(hyperlite::CommandType::kPutPixel, xs[i], ys[i], 0, 0, packed);
	}
	self->native_engine->PushCommandsRange(batch.data(), batch.size());
	Py_RETURN_NONE;
}

/**
 * Queue a packed DrawCommand array in one native crossing.
 */
static PyObject* PyEngine_push_commands(PyEngineObject* self, PyObject* args) {
	PyObject* src_obj = nullptr;
	if (!PyArg_ParseTuple(args, "O", &src_obj)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(src_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "buffer must expose a contiguous readonly byte view.");
		return nullptr;
	}
	const std::size_t bytes = static_cast<std::size_t>(view.len);
	if (bytes % sizeof(DrawCommand) != 0) {
		PyBuffer_Release(&view);
		PyErr_Format(PyExc_ValueError, "buffer size must be a multiple of %zu bytes.", sizeof(DrawCommand));
		return nullptr;
	}
	const std::size_t count = bytes / sizeof(DrawCommand);
	if (count == 0) {
		PyBuffer_Release(&view);
		Py_RETURN_NONE;
	}
	self->native_engine->PushCommandsRange(reinterpret_cast<const DrawCommand*>(view.buf), count);
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

/**
 * Blit RGBA image data natively without per-pixel command expansion.
 */
static PyObject* PyEngine_blit_rgba(PyEngineObject* self, PyObject* args) {
	PyObject* src_obj = nullptr;
	int dst_x = 0;
	int dst_y = 0;
	int width = 0;
	int height = 0;
	if (!PyArg_ParseTuple(args, "Oiiii", &src_obj, &dst_x, &dst_y, &width, &height)) {
		return nullptr;
	}
	if (width <= 0 || height <= 0) {
		PyErr_SetString(PyExc_ValueError, "width and height must be positive.");
		return nullptr;
	}

	Py_buffer view{};
	if (PyObject_GetBuffer(src_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "buffer must expose a contiguous readonly byte view.");
		return nullptr;
	}

	const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
	const std::size_t required_bytes = pixel_count * 4U;
	if (static_cast<std::size_t>(view.len) < required_bytes) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "buffer is smaller than width * height * 4 bytes.");
		return nullptr;
	}
	self->native_engine->BlitRgba(
		reinterpret_cast<const std::uint8_t*>(view.buf),
		static_cast<std::size_t>(view.len),
		dst_x,
		dst_y,
		width,
		height);
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

/**
 * Load a resident RGBA8 atlas and return a handle.
 */
static PyObject* PyEngine_load_atlas(PyEngineObject* self, PyObject* args) {
	PyObject* src_obj = nullptr;
	int width = 0;
	int height = 0;
	if (!PyArg_ParseTuple(args, "Oii", &src_obj, &width, &height)) {
		return nullptr;
	}
	if (width <= 0 || height <= 0) {
		PyErr_SetString(PyExc_ValueError, "width and height must be positive.");
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(src_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "buffer must expose a contiguous readonly byte view.");
		return nullptr;
	}
	const int handle = self->native_engine->LoadAtlas(
		reinterpret_cast<const std::uint8_t*>(view.buf),
		static_cast<std::size_t>(view.len),
		width,
		height);
	PyBuffer_Release(&view);
	if (handle < 0) {
		PyErr_SetString(PyExc_ValueError, "atlas upload failed.");
		return nullptr;
	}
	return PyLong_FromLong(handle);
}

/**
 * Draw a sub-rectangle from a loaded atlas.
 */
static PyObject* PyEngine_draw_sprite(PyEngineObject* self, PyObject* args) {
	int atlas_id = 0;
	int src_x = 0;
	int src_y = 0;
	int width = 0;
	int height = 0;
	int dst_x = 0;
	int dst_y = 0;
	if (!PyArg_ParseTuple(args, "iiiiiii", &atlas_id, &src_x, &src_y, &width, &height, &dst_x, &dst_y)) {
		return nullptr;
	}
	self->native_engine->DrawSprite(atlas_id, src_x, src_y, width, height, dst_x, dst_y);
	Py_RETURN_NONE;
}

/**
 * Enable or disable partial Win32 presents for dirty regions.
 */
static PyObject* PyEngine_set_dirty_present(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "i", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetDirtyPresent(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Enable DXGI flip-model present (default on). Falls back to GDI when unavailable.
 */
static PyObject* PyEngine_set_dxgi_present(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetDxgiPresent(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Return whether DXGI swapchain present is enabled.
 */
static PyObject* PyEngine_dxgi_present_enabled(PyEngineObject* self, PyObject* args) {
	(void)args;
	return PyBool_FromLong(self->native_engine->DxgiPresentEnabled() ? 1 : 0);
}

/**
 * Enable GPU-direct CUDA→DXGI presentation (skips CPU readback on GPU backend).
 */
static PyObject* PyEngine_set_direct_present(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetDirectPresent(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Configure blit material-sort threshold (0 disables sorting).
 */
static PyObject* PyEngine_set_blit_sort_threshold(PyEngineObject* self, PyObject* args) {
	Py_ssize_t threshold = 256;
	if (!PyArg_ParseTuple(args, "n", &threshold)) {
		return nullptr;
	}
	self->native_engine->SetBlitSortThreshold(static_cast<std::size_t>(threshold));
	Py_RETURN_NONE;
}

/**
 * Configure line material-sort threshold (0 disables sorting).
 */
static PyObject* PyEngine_set_line_sort_threshold(PyEngineObject* self, PyObject* args) {
	Py_ssize_t threshold = 64;
	if (!PyArg_ParseTuple(args, "n", &threshold)) {
		return nullptr;
	}
	self->native_engine->SetLineSortThreshold(static_cast<std::size_t>(threshold));
	Py_RETURN_NONE;
}

/**
 * Pre-reserve command-buffer capacity (grows automatically when exceeded).
 */
static PyObject* PyEngine_set_command_buffer_reserve(PyEngineObject* self, PyObject* args) {
	Py_ssize_t capacity = 0;
	if (!PyArg_ParseTuple(args, "n", &capacity)) {
		return nullptr;
	}
	if (capacity < 0) {
		PyErr_SetString(PyExc_ValueError, "capacity must be non-negative.");
		return nullptr;
	}
	self->native_engine->SetCommandBufferReserve(static_cast<std::size_t>(capacity));
	Py_RETURN_NONE;
}

/**
 * Enable or disable vertical sync on present.
 */
static PyObject* PyEngine_set_vsync(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetVsync(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Return whether vertical sync is enabled.
 */
static PyObject* PyEngine_vsync_enabled(PyEngineObject* self, PyObject* args) {
	(void)args;
	if (self->native_engine->VsyncEnabled()) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Capture the current command buffer as a reusable retained layer handle.
 */
static PyObject* PyEngine_commit_retained_layer(PyEngineObject* self, PyObject* args) {
	(void)args;
	return PyLong_FromLong(self->native_engine->CommitRetainedLayer());
}

/**
 * Replay a retained layer into the active frame.
 */
static PyObject* PyEngine_draw_retained_layer(PyEngineObject* self, PyObject* args) {
	int layer_handle = 0;
	if (!PyArg_ParseTuple(args, "i", &layer_handle)) {
		return nullptr;
	}
	self->native_engine->DrawRetainedLayer(layer_handle);
	Py_RETURN_NONE;
}

/**
 * Fused poll + clear + sprite batch + present in one native call.
 *
 * sprite_buffer: contiguous array of SpriteDrawDesc (7 x int32 per sprite).
 */
static PyObject* PyEngine_tick_blits(PyEngineObject* self, PyObject* args) {
	PyObject* sprite_buffer = nullptr;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "Oiii|i", &sprite_buffer, &r, &g, &b, &a)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(sprite_buffer, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "sprite_buffer must expose a contiguous readonly byte view.");
		return nullptr;
	}
	const std::size_t desc_bytes = sizeof(hyperlite::SpriteDrawDesc);
	if (view.len % static_cast<Py_ssize_t>(desc_bytes) != 0) {
		PyBuffer_Release(&view);
		PyErr_Format(PyExc_ValueError, "sprite_buffer size must be a multiple of %zu bytes.", desc_bytes);
		return nullptr;
	}
	const std::size_t sprite_count = static_cast<std::size_t>(view.len) / desc_bytes;
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_count = self->native_engine->TickBlits(
		hyperlite::PackColor(color),
		reinterpret_cast<const hyperlite::SpriteDrawDesc*>(view.buf),
		sprite_count);
	PyBuffer_Release(&view);
	return PyLong_FromLong(draw_count);
}

/**
 * Fused poll + clear + parallel wireframe raster + present in one native call.
 *
 * segments: contiguous int32 array with line_count * 4 values (x0,y0,x1,y1,...).
 */
static PyObject* PyEngine_tick_lines(PyEngineObject* self, PyObject* args) {
	PyObject* segments_obj = nullptr;
	int clear_r = 0;
	int clear_g = 0;
	int clear_b = 0;
	int clear_a = 255;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	int width = 1;
	if (!PyArg_ParseTuple(args, "Oiiiiiiii|i", &segments_obj, &clear_r, &clear_g, &clear_b, &clear_a, &r, &g, &b, &a, &width)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(segments_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "segments must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 4) != 0) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "segments length must be a multiple of 4 int32 values per line.");
		return nullptr;
	}
	const std::size_t line_count = static_cast<std::size_t>(view.len) / (sizeof(std::int32_t) * 4U);
	const Color clear_color{
		static_cast<std::uint8_t>(clear_r),
		static_cast<std::uint8_t>(clear_g),
		static_cast<std::uint8_t>(clear_b),
		static_cast<std::uint8_t>(clear_a)};
	const Color line_color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_count = self->native_engine->TickLinesPoll(
		hyperlite::PackColor(clear_color),
		reinterpret_cast<const std::int32_t*>(view.buf),
		line_count,
		hyperlite::PackColor(line_color),
		width);
	PyBuffer_Release(&view);
	return PyLong_FromLong(draw_count);
}

/**
 * Fused poll + clear + GPU line batch + present in one native call.
 */
static PyObject* PyEngine_tick_lines_gpu(PyEngineObject* self, PyObject* args) {
	PyObject* segments_obj = nullptr;
	int clear_r = 0;
	int clear_g = 0;
	int clear_b = 0;
	int clear_a = 255;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	int width = 1;
	if (!PyArg_ParseTuple(args, "Oiiiiiiii|i", &segments_obj, &clear_r, &clear_g, &clear_b, &clear_a, &r, &g, &b, &a, &width)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(segments_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "segments must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 4) != 0) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "segments length must be a multiple of 4 int32 values per line.");
		return nullptr;
	}
	const std::size_t line_count = static_cast<std::size_t>(view.len) / (sizeof(std::int32_t) * 4U);
	const Color clear_color{
		static_cast<std::uint8_t>(clear_r),
		static_cast<std::uint8_t>(clear_g),
		static_cast<std::uint8_t>(clear_b),
		static_cast<std::uint8_t>(clear_a)};
	const Color line_color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_count = self->native_engine->TickLinesGpu(
		hyperlite::PackColor(clear_color),
		reinterpret_cast<const std::int32_t*>(view.buf),
		line_count,
		hyperlite::PackColor(line_color),
		width);
	PyBuffer_Release(&view);
	return PyLong_FromLong(draw_count);
}

/**
 * Fused poll + clear + parallel wireframe raster + present (explicit poll path).
 */
static PyObject* PyEngine_tick_lines_poll(PyEngineObject* self, PyObject* args) {
	PyObject* segments_obj = nullptr;
	int clear_r = 0;
	int clear_g = 0;
	int clear_b = 0;
	int clear_a = 255;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	int width = 1;
	if (!PyArg_ParseTuple(args, "Oiiiiiiii|i", &segments_obj, &clear_r, &clear_g, &clear_b, &clear_a, &r, &g, &b, &a, &width)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(segments_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "segments must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 4) != 0) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "segments length must be a multiple of 4 int32 values per line.");
		return nullptr;
	}
	const std::size_t line_count = static_cast<std::size_t>(view.len) / (sizeof(std::int32_t) * 4U);
	const Color clear_color{
		static_cast<std::uint8_t>(clear_r),
		static_cast<std::uint8_t>(clear_g),
		static_cast<std::uint8_t>(clear_b),
		static_cast<std::uint8_t>(clear_a)};
	const Color line_color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	const int draw_count = self->native_engine->TickLinesPoll(
		hyperlite::PackColor(clear_color),
		reinterpret_cast<const std::int32_t*>(view.buf),
		line_count,
		hyperlite::PackColor(line_color),
		width);
	PyBuffer_Release(&view);
	return PyLong_FromLong(draw_count);
}

/**
 * Queue many line commands from one int32 segment buffer.
 */
static PyObject* PyEngine_lines_bulk(PyEngineObject* self, PyObject* args) {
	PyObject* segments_obj = nullptr;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	int width = 1;
	if (!PyArg_ParseTuple(args, "Oiii|ii", &segments_obj, &r, &g, &b, &a, &width)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(segments_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "segments must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 4) != 0) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "segments length must be a multiple of 4 int32 values per line.");
		return nullptr;
	}
	const std::size_t line_count = static_cast<std::size_t>(view.len) / (sizeof(std::int32_t) * 4U);
	const Color line_color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	self->native_engine->LinesBulk(
		reinterpret_cast<const std::int32_t*>(view.buf),
		line_count,
		hyperlite::PackColor(line_color),
		width);
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

/**
 * Queue many line commands with per-segment packed RGBA colors.
 */
static PyObject* PyEngine_lines_bulk_colored(PyEngineObject* self, PyObject* args) {
	PyObject* segments_obj = nullptr;
	PyObject* colors_obj = nullptr;
	int width = 1;
	if (!PyArg_ParseTuple(args, "OO|i", &segments_obj, &colors_obj, &width)) {
		return nullptr;
	}
	Py_buffer seg_view{};
	if (PyObject_GetBuffer(segments_obj, &seg_view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "segments must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (seg_view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 4) != 0) {
		PyBuffer_Release(&seg_view);
		PyErr_SetString(PyExc_ValueError, "segments length must be a multiple of 4 int32 values per line.");
		return nullptr;
	}
	const std::size_t line_count = static_cast<std::size_t>(seg_view.len) / (sizeof(std::int32_t) * 4U);

	Py_buffer color_view{};
	if (PyObject_GetBuffer(colors_obj, &color_view, PyBUF_CONTIG_RO) != 0) {
		PyBuffer_Release(&seg_view);
		PyErr_SetString(PyExc_TypeError, "colors must expose a contiguous readonly byte view.");
		return nullptr;
	}
	const std::size_t color_count = static_cast<std::size_t>(color_view.len) / sizeof(std::uint32_t);
	if (color_count < line_count) {
		PyBuffer_Release(&color_view);
		PyBuffer_Release(&seg_view);
		PyErr_SetString(PyExc_ValueError, "colors must have at least one uint32 per line segment.");
		return nullptr;
	}
	self->native_engine->LinesBulkColored(
		reinterpret_cast<const std::int32_t*>(seg_view.buf),
		reinterpret_cast<const std::uint32_t*>(color_view.buf),
		line_count,
		width);
	PyBuffer_Release(&color_view);
	PyBuffer_Release(&seg_view);
	Py_RETURN_NONE;
}

/**
 * Queue many put-pixel commands from interleaved int32 x,y pairs.
 */
static PyObject* PyEngine_put_pixels_buffer(PyEngineObject* self, PyObject* args) {
	PyObject* xy_obj = nullptr;
	int r = 0;
	int g = 0;
	int b = 0;
	int a = 255;
	if (!PyArg_ParseTuple(args, "Oiii|i", &xy_obj, &r, &g, &b, &a)) {
		return nullptr;
	}
	Py_buffer view{};
	if (PyObject_GetBuffer(xy_obj, &view, PyBUF_CONTIG_RO) != 0) {
		PyErr_SetString(PyExc_TypeError, "xy_pairs must expose a contiguous readonly byte view.");
		return nullptr;
	}
	if (view.len % (static_cast<Py_ssize_t>(sizeof(std::int32_t)) * 2) != 0) {
		PyBuffer_Release(&view);
		PyErr_SetString(PyExc_ValueError, "xy_pairs length must be a multiple of 2 int32 values per pixel.");
		return nullptr;
	}
	const std::size_t count = static_cast<std::size_t>(view.len) / (sizeof(std::int32_t) * 2U);
	const Color color{
		static_cast<std::uint8_t>(r),
		static_cast<std::uint8_t>(g),
		static_cast<std::uint8_t>(b),
		static_cast<std::uint8_t>(a)};
	self->native_engine->PutPixelsBuffer(
		reinterpret_cast<const std::int32_t*>(view.buf),
		count,
		hyperlite::PackColor(color));
	PyBuffer_Release(&view);
	Py_RETURN_NONE;
}

/**
 * Return a writable memoryview over the host framebuffer (RGBA8).
 */
static PyObject* PyEngine_framebuffer_ptr(PyEngineObject* self, PyObject* args) {
	(void)args;
	std::uint8_t* ptr = self->native_engine->FramebufferPtr();
	const std::size_t bytes = self->native_engine->FramebufferBytes();
	if (ptr == nullptr || bytes == 0U) {
		return PyMemoryView_FromMemory(nullptr, 0, PyBUF_WRITE);
	}
	return PyMemoryView_FromMemory(reinterpret_cast<char*>(ptr), static_cast<Py_ssize_t>(bytes), PyBUF_WRITE);
}

/**
 * Replay a retained layer on the active backend device.
 */
static PyObject* PyEngine_draw_retained_layer_gpu(PyEngineObject* self, PyObject* args) {
	int layer_handle = 0;
	if (!PyArg_ParseTuple(args, "i", &layer_handle)) {
		return nullptr;
	}
	return PyLong_FromLong(self->native_engine->DrawRetainedLayerGpu(layer_handle));
}

/**
 * Return last wireframe timing tuple: (raster_ms, present_ms).
 */
static PyObject* PyEngine_wireframe_timings(PyEngineObject* self, PyObject* args) {
	(void)args;
	const hyperlite::WireframeTimings timings = self->native_engine->WireframeTimingsLast();
	return Py_BuildValue("(dd)", static_cast<double>(timings.raster_ms), static_cast<double>(timings.present_ms));
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
	int width = 1;
	if (!PyArg_ParseTuple(args, "iiiiiii|ii", &x0, &y0, &x1, &y1, &r, &g, &b, &a, &width)) {
		return nullptr;
	}
	const auto color = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(a)};
	self->native_engine->PushCommand(MakeLineCommand(x0, y0, x1, y1, color, width));
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
 * Capture or release the mouse for FPS-style relative movement.
 */
static PyObject* PyEngine_set_mouse_captured(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetMouseCaptured(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Return whether the mouse is currently captured.
 */
static PyObject* PyEngine_mouse_captured(PyEngineObject* self, PyObject* args) {
	(void)args;
	if (self->native_engine->MouseCaptured()) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Return relative mouse motion since the last poll_events (meaningful while captured).
 */
static PyObject* PyEngine_mouse_delta(PyEngineObject* self, PyObject* args) {
	(void)args;
	const auto& delta = self->native_engine->GetInputState().mouse_delta;
	return Py_BuildValue("(ii)", delta.x, delta.y);
}

/**
 * Return whether a mouse button is currently held (see MouseButtons).
 */
static PyObject* PyEngine_mouse_button_down(PyEngineObject* self, PyObject* args) {
	int button = 0;
	if (!PyArg_ParseTuple(args, "i", &button)) {
		return nullptr;
	}
	if (button < 0 || button >= static_cast<int>(hyperlite::kMouseButtonCount)) {
		PyErr_SetString(PyExc_ValueError, "button must be 0..4 (MouseButtons.*).");
		return nullptr;
	}
	if (self->native_engine->GetInputState().mouse_buttons[static_cast<std::size_t>(button)]) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Enter or leave borderless fullscreen.
 */
static PyObject* PyEngine_set_fullscreen(PyEngineObject* self, PyObject* args) {
	int enabled = 0;
	if (!PyArg_ParseTuple(args, "p", &enabled)) {
		return nullptr;
	}
	self->native_engine->SetFullscreen(enabled != 0);
	Py_RETURN_NONE;
}

/**
 * Return whether borderless fullscreen is active.
 */
static PyObject* PyEngine_is_fullscreen(PyEngineObject* self, PyObject* args) {
	(void)args;
	if (self->native_engine->IsFullscreen()) {
		Py_RETURN_TRUE;
	}
	Py_RETURN_FALSE;
}

/**
 * Return current framebuffer / client size in pixels.
 */
static PyObject* PyEngine_window_size(PyEngineObject* self, PyObject* args) {
	(void)args;
	int width = 0;
	int height = 0;
	self->native_engine->WindowSize(width, height);
	return Py_BuildValue("(ii)", width, height);
}

/**
 * Resize the window client area and internal framebuffer.
 */
static PyObject* PyEngine_set_window_size(PyEngineObject* self, PyObject* args) {
	int width = 0;
	int height = 0;
	if (!PyArg_ParseTuple(args, "ii", &width, &height)) {
		return nullptr;
	}
	if (width <= 0 || height <= 0) {
		PyErr_SetString(PyExc_ValueError, "width and height must be positive.");
		return nullptr;
	}
	self->native_engine->SetWindowSize(width, height);
	Py_RETURN_NONE;
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
 * Enable/disable one-frame-deep double-buffered present (CPU and GPU backends).
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
 * Alias for set_pipelined — enables double-buffered present.
 */
static PyObject* PyEngine_set_double_buffered_present(PyEngineObject* self, PyObject* args) {
	return PyEngine_set_pipelined(self, args);
}

/**
 * Return last-frame timing tuple:
 * (record_ms, upload_ms, kernel_ms, readback_ms, present_ms).
 */
static PyObject* PyEngine_gpu_timings(PyEngineObject* self, PyObject* args) {
	(void)args;
	const hyperlite::GpuTimings timings = self->native_engine->GpuTimingsLast();
	return Py_BuildValue(
		"(ddddd)",
		static_cast<double>(timings.record_ms),
		static_cast<double>(timings.upload_ms),
		static_cast<double>(timings.kernel_ms),
		static_cast<double>(timings.readback_ms),
		static_cast<double>(timings.present_ms));
}

/**
 * Return frame delta time in seconds.
 */
static PyObject* PyEngine_delta_time(PyEngineObject* self, PyObject* args) {
	(void)args;
	return PyFloat_FromDouble(self->native_engine->DeltaTime());
}

static PyMethodDef PyEngine_methods[] = {
	{"begin_frame", reinterpret_cast<PyCFunction>(PyEngine_begin_frame), METH_NOARGS, "Begin frame command recording."},
	{"end_frame", reinterpret_cast<PyCFunction>(PyEngine_end_frame), METH_NOARGS, "Execute queued commands."},
	{"present", reinterpret_cast<PyCFunction>(PyEngine_present), METH_NOARGS, "Present current framebuffer."},
	{"tick", reinterpret_cast<PyCFunction>(PyEngine_tick), METH_NOARGS, "Poll events, execute end_frame, and present."},
	{"poll_events", reinterpret_cast<PyCFunction>(PyEngine_poll_events), METH_NOARGS, "Pump platform events."},
	{"is_running", reinterpret_cast<PyCFunction>(PyEngine_is_running), METH_NOARGS, "Return whether the window is alive."},
	{"backend_name", reinterpret_cast<PyCFunction>(PyEngine_backend_name), METH_NOARGS, "Return active backend name."},
	{"upload_frame_rgba", reinterpret_cast<PyCFunction>(PyEngine_upload_frame_rgba), METH_VARARGS, "Upload full RGBA8 frame as base image."},
	{"clear", reinterpret_cast<PyCFunction>(PyEngine_clear), METH_VARARGS, "Queue clear command."},
	{"put_pixel", reinterpret_cast<PyCFunction>(PyEngine_put_pixel), METH_VARARGS, "Queue pixel command."},
	{"put_pixels", reinterpret_cast<PyCFunction>(PyEngine_put_pixels), METH_VARARGS, "Queue many pixel commands in one call."},
	{"push_commands", reinterpret_cast<PyCFunction>(PyEngine_push_commands), METH_VARARGS, "Queue packed DrawCommand array from a buffer."},
	{"blit_rgba", reinterpret_cast<PyCFunction>(PyEngine_blit_rgba), METH_VARARGS, "Queue deferred RGBA blit."},
	{"load_atlas", reinterpret_cast<PyCFunction>(PyEngine_load_atlas), METH_VARARGS, "Load resident RGBA8 atlas; returns handle."},
	{"draw_sprite", reinterpret_cast<PyCFunction>(PyEngine_draw_sprite), METH_VARARGS, "Draw atlas sub-rectangle."},
	{"set_dirty_present", reinterpret_cast<PyCFunction>(PyEngine_set_dirty_present), METH_VARARGS, "Enable partial GDI presents for dirty regions (GDI fallback only)."},
	{"set_dxgi_present", reinterpret_cast<PyCFunction>(PyEngine_set_dxgi_present), METH_VARARGS, "Enable DXGI flip-model present (default on)."},
	{"dxgi_present_enabled", reinterpret_cast<PyCFunction>(PyEngine_dxgi_present_enabled), METH_NOARGS, "Return whether DXGI present is enabled."},
	{"set_direct_present", reinterpret_cast<PyCFunction>(PyEngine_set_direct_present), METH_VARARGS, "Enable GPU CUDA→DXGI present without CPU readback."},
	{"set_blit_sort_threshold", reinterpret_cast<PyCFunction>(PyEngine_set_blit_sort_threshold), METH_VARARGS, "Set blit material-sort threshold (0 disables)."},
	{"set_line_sort_threshold", reinterpret_cast<PyCFunction>(PyEngine_set_line_sort_threshold), METH_VARARGS, "Set line color-sort threshold (0 disables)."},
	{"set_command_buffer_reserve", reinterpret_cast<PyCFunction>(PyEngine_set_command_buffer_reserve), METH_VARARGS, "Pre-reserve command buffer capacity."},
	{"set_vsync", reinterpret_cast<PyCFunction>(PyEngine_set_vsync), METH_VARARGS, "Enable or disable vertical sync."},
	{"vsync_enabled", reinterpret_cast<PyCFunction>(PyEngine_vsync_enabled), METH_NOARGS, "Return whether vsync is enabled."},
	{"commit_retained_layer", reinterpret_cast<PyCFunction>(PyEngine_commit_retained_layer), METH_NOARGS, "Capture current commands as a retained layer handle."},
	{"draw_retained_layer", reinterpret_cast<PyCFunction>(PyEngine_draw_retained_layer), METH_VARARGS, "Replay a retained layer into the active frame."},
	{"tick_blits", reinterpret_cast<PyCFunction>(PyEngine_tick_blits), METH_VARARGS, "Poll + clear + sprite batch + present in one call."},
	{"tick_lines", reinterpret_cast<PyCFunction>(PyEngine_tick_lines), METH_VARARGS, "Poll + clear + parallel wireframe batch + present in one call."},
	{"tick_lines_poll", reinterpret_cast<PyCFunction>(PyEngine_tick_lines_poll), METH_VARARGS, "Poll + clear + parallel CPU wireframe + present."},
	{"tick_lines_gpu", reinterpret_cast<PyCFunction>(PyEngine_tick_lines_gpu), METH_VARARGS, "Poll + clear + GPU line batch + present in one call."},
	{"lines_bulk", reinterpret_cast<PyCFunction>(PyEngine_lines_bulk), METH_VARARGS, "Queue many lines from one int32 segment buffer."},
	{"lines_bulk_colored", reinterpret_cast<PyCFunction>(PyEngine_lines_bulk_colored), METH_VARARGS, "Queue many lines with per-segment packed colors."},
	{"put_pixels_buffer", reinterpret_cast<PyCFunction>(PyEngine_put_pixels_buffer), METH_VARARGS, "Queue many pixels from interleaved int32 x,y pairs."},
	{"framebuffer_ptr", reinterpret_cast<PyCFunction>(PyEngine_framebuffer_ptr), METH_NOARGS, "Writable memoryview over host RGBA8 framebuffer."},
	{"draw_retained_layer_gpu", reinterpret_cast<PyCFunction>(PyEngine_draw_retained_layer_gpu), METH_VARARGS, "Replay a retained layer on the active backend device."},
	{"wireframe_timings", reinterpret_cast<PyCFunction>(PyEngine_wireframe_timings), METH_NOARGS, "Return last-frame (raster_ms, present_ms)."},
	{"line", reinterpret_cast<PyCFunction>(PyEngine_line), METH_VARARGS, "Queue line command."},
	{"rect_fill", reinterpret_cast<PyCFunction>(PyEngine_rect_fill), METH_VARARGS, "Queue filled rectangle command."},
	{"rect_outline", reinterpret_cast<PyCFunction>(PyEngine_rect_outline), METH_VARARGS, "Queue outline rectangle command."},
	{"key_down", reinterpret_cast<PyCFunction>(PyEngine_key_down), METH_VARARGS, "Read key state by virtual key code."},
	{"mouse_pos", reinterpret_cast<PyCFunction>(PyEngine_mouse_pos), METH_NOARGS, "Return mouse position."},
	{"set_mouse_captured", reinterpret_cast<PyCFunction>(PyEngine_set_mouse_captured), METH_VARARGS, "Capture or release the mouse cursor."},
	{"mouse_captured", reinterpret_cast<PyCFunction>(PyEngine_mouse_captured), METH_NOARGS, "Return whether the mouse is captured."},
	{"mouse_delta", reinterpret_cast<PyCFunction>(PyEngine_mouse_delta), METH_NOARGS, "Return relative mouse motion since last poll_events."},
	{"mouse_button_down", reinterpret_cast<PyCFunction>(PyEngine_mouse_button_down), METH_VARARGS, "Return whether a mouse button is held (MouseButtons.*)."},
	{"set_fullscreen", reinterpret_cast<PyCFunction>(PyEngine_set_fullscreen), METH_VARARGS, "Enter or leave borderless fullscreen."},
	{"is_fullscreen", reinterpret_cast<PyCFunction>(PyEngine_is_fullscreen), METH_NOARGS, "Return whether fullscreen is active."},
	{"window_size", reinterpret_cast<PyCFunction>(PyEngine_window_size), METH_NOARGS, "Return (width, height) of the framebuffer."},
	{"set_window_size", reinterpret_cast<PyCFunction>(PyEngine_set_window_size), METH_VARARGS, "Resize window client area and framebuffer."},
	{"spiro_object", reinterpret_cast<PyCFunction>(PyEngine_spiro_object), METH_VARARGS, "Queue one native spirograph object and return line count."},
	{"spiro_scene", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene), METH_VARARGS, "Queue full native spirograph benchmark scene and return line count."},
	{"spiro_scene_fast", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene_fast), METH_VARARGS, "Queue full native spirograph scene without return allocation."},
	{"supports_gpu_scene", reinterpret_cast<PyCFunction>(PyEngine_supports_gpu_scene), METH_NOARGS, "Return whether the backend runs scenes on the GPU."},
	{"clear_gpu", reinterpret_cast<PyCFunction>(PyEngine_clear_gpu), METH_VARARGS, "Clear the device framebuffer directly on the GPU."},
	{"spiro_scene_cuda", reinterpret_cast<PyCFunction>(PyEngine_spiro_scene_cuda), METH_VARARGS, "Generate and rasterize the spiro scene fully on the GPU."},
	{"spiro_frame_cuda", reinterpret_cast<PyCFunction>(PyEngine_spiro_frame_cuda), METH_VARARGS, "Clear + scene via captured CUDA graph; returns segment count."},
	{"spiro_frame_direct", reinterpret_cast<PyCFunction>(PyEngine_spiro_frame_direct), METH_VARARGS, "Clear + scene via direct GPU launches; returns segment count."},
	{"tick_gpu_spiro", reinterpret_cast<PyCFunction>(PyEngine_tick_gpu_spiro), METH_VARARGS, "Poll + fused clear/scene + present in one native call."},
	{"gpu_timings", reinterpret_cast<PyCFunction>(PyEngine_gpu_timings), METH_NOARGS, "Return last-frame (record_ms, upload_ms, kernel_ms, readback_ms, present_ms)."},
	{"delta_time", reinterpret_cast<PyCFunction>(PyEngine_delta_time), METH_NOARGS, "Seconds since previous begin_frame."},
	{"set_pipelined", reinterpret_cast<PyCFunction>(PyEngine_set_pipelined), METH_VARARGS, "Enable/disable double-buffered present (+1 frame latency, CPU async GDI or GPU pipelined readback)."},
	{"set_double_buffered_present", reinterpret_cast<PyCFunction>(PyEngine_set_double_buffered_present), METH_VARARGS, "Alias for set_pipelined."},
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

	PyObject* keys = PyModule_New("hyperlite.Keys");
	if (!keys) {
		Py_DECREF(module);
		return nullptr;
	}
	PyModule_AddIntConstant(keys, "Escape", 0x1B);
	PyModule_AddIntConstant(keys, "Tab", 0x09);
	PyModule_AddIntConstant(keys, "W", 0x57);
	PyModule_AddIntConstant(keys, "A", 0x41);
	PyModule_AddIntConstant(keys, "S", 0x53);
	PyModule_AddIntConstant(keys, "D", 0x44);
	PyModule_AddIntConstant(keys, "F11", 0x7A);
	PyModule_AddIntConstant(keys, "Return", 0x0D);
	if (PyModule_AddObject(module, "Keys", keys) < 0) {
		Py_DECREF(keys);
		Py_DECREF(module);
		return nullptr;
	}

	PyObject* mouse_buttons = PyModule_New("hyperlite.MouseButtons");
	if (!mouse_buttons) {
		Py_DECREF(module);
		return nullptr;
	}
	PyModule_AddIntConstant(mouse_buttons, "Left", static_cast<int>(hyperlite::MouseButton::Left));
	PyModule_AddIntConstant(mouse_buttons, "Right", static_cast<int>(hyperlite::MouseButton::Right));
	PyModule_AddIntConstant(mouse_buttons, "Middle", static_cast<int>(hyperlite::MouseButton::Middle));
	PyModule_AddIntConstant(mouse_buttons, "X1", static_cast<int>(hyperlite::MouseButton::X1));
	PyModule_AddIntConstant(mouse_buttons, "X2", static_cast<int>(hyperlite::MouseButton::X2));
	if (PyModule_AddObject(module, "MouseButtons", mouse_buttons) < 0) {
		Py_DECREF(mouse_buttons);
		Py_DECREF(module);
		return nullptr;
	}

	return module;
}
