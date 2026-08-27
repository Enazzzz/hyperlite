#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"
#include "engine/types.hpp"

namespace {

/**
 * Assert helper for deterministic triangle raster tests.
 */
bool Expect(const bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		return false;
	}
	return true;
}

/**
 * Read one packed RGBA pixel from the engine framebuffer.
 */
hyperlite::Color ReadPixel(hyperlite::Engine& engine, const int x, const int y) {
	int width = 0;
	int height = 0;
	engine.WindowSize(width, height);
	const std::uint8_t* data = engine.FramebufferPtr();
	const std::size_t idx = static_cast<std::size_t>((y * width + x) * 4);
	return {
		data[idx + 0U],
		data[idx + 1U],
		data[idx + 2U],
		data[idx + 3U]};
}

/**
 * Whether a pixel matches an expected RGB (alpha ignored).
 */
bool PixelRgbEquals(const hyperlite::Color& pixel, const std::uint8_t r, const std::uint8_t g, const std::uint8_t b) {
	return pixel.r == r && pixel.g == g && pixel.b == b;
}

/**
 * Identity column-major 4x4 (world = clip).
 */
void FillIdentity(float* m) {
	for (int i = 0; i < 16; ++i) {
		m[i] = 0.0f;
	}
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

} // namespace

/**
 * Unit tests for tiled filled-triangle raster (Layer 1, headless CPU).
 */
int main() {
	bool ok = true;

	// --- Screen-space: one triangle covers known pixels; outside stays clear ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-screen", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);

		// Large CCW-in-screen triangle covering the center (cull off on screen path).
		const float verts[] = {
			8.0f, 8.0f, 0.0f,
			56.0f, 8.0f, 0.0f,
			32.0f, 56.0f, 0.0f};
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({20, 20, 20, 255})));
		engine.TrisScreen(verts, 1U, hyperlite::PackColor({255, 0, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 24), 255, 0, 0), "screen: interior pixel red") && ok;
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 2, 2), 20, 20, 20), "screen: outside stays clear") && ok;
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 62, 62), 20, 20, 20), "screen: far corner clear") && ok;
	}

	// --- Depth: two overlapping screen tris; nearer wins ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-depth", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);

		// Far blue then near yellow; both cover (32,32).
		const float far_tri[] = {
			10.0f, 10.0f, 0.5f,
			54.0f, 10.0f, 0.5f,
			32.0f, 54.0f, 0.5f};
		const float near_tri[] = {
			16.0f, 16.0f, -0.5f,
			48.0f, 16.0f, -0.5f,
			32.0f, 48.0f, -0.5f};

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.TrisScreen(far_tri, 1U, hyperlite::PackColor({0, 0, 255, 255}));
		engine.TrisScreen(near_tri, 1U, hyperlite::PackColor({255, 255, 0, 255}));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 28), 255, 255, 0), "depth: nearer yellow wins") && ok;
		const float d = engine.DepthAt(32, 28);
		ok = Expect(d < 0.5f, "depth: window depth nearer than mid") && ok;
	}

	// --- Near clip: world tri with one vertex behind camera must not crash; visible part draws ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-near", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);
		engine.SetCullBackfaces(false); // winding after clip may flip; test clip robustness

		// Two verts in front, one behind near (z < -w). Must clip without crash.
		const float verts[] = {
			-0.4f, -0.4f, 0.0f,
			0.4f, -0.4f, 0.0f,
			0.0f, 0.4f, -2.0f};
		engine.TickTris3d(
			hyperlite::PackColor({10, 10, 10, 255}),
			verts,
			1U,
			hyperlite::PackColor({0, 255, 0, 255}));

		bool any_green = false;
		bool any_garbage = false;
		const std::uint8_t* data = engine.FramebufferPtr();
		for (int y = 0; y < 64; ++y) {
			for (int x = 0; x < 64; ++x) {
				const std::size_t i = static_cast<std::size_t>((y * 64 + x) * 4);
				const std::uint8_t r = data[i];
				const std::uint8_t g = data[i + 1U];
				const std::uint8_t b = data[i + 2U];
				const bool clear = (r == 10 && g == 10 && b == 10);
				const bool green = (r == 0 && g == 255 && b == 0);
				if (green) {
					any_green = true;
				}
				if (!clear && !green) {
					any_garbage = true;
				}
			}
		}
		ok = Expect(any_green, "near-clip: visible part still drawn") && ok;
		ok = Expect(!any_garbage, "near-clip: no garbage pixels") && ok;
	}

	// --- Backface: OpenGL-front (CCW in NDC) draws; flipped winding culled ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-cull", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);
		engine.SetCullBackfaces(true);

		// CCW in NDC (OpenGL front) → CW after Y flip → kept.
		const float front[] = {
			-0.5f, -0.5f, 0.0f,
			0.5f, -0.5f, 0.0f,
			0.0f, 0.5f, 0.0f};
		// CW in NDC → culled.
		const float back[] = {
			-0.5f, -0.5f, 0.0f,
			0.0f, 0.5f, 0.0f,
			0.5f, -0.5f, 0.0f};

		engine.TickTris3d(
			hyperlite::PackColor({0, 0, 0, 255}),
			front,
			1U,
			hyperlite::PackColor({255, 0, 0, 255}));
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 255, 0, 0), "cull: front face draws") && ok;

		engine.TickTris3d(
			hyperlite::PackColor({0, 0, 0, 255}),
			back,
			1U,
			hyperlite::PackColor({0, 255, 0, 255}));
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 0, 0, 0), "cull: back face discarded") && ok;
	}

	// --- Shared edge: two tris sharing an edge; no holes (top-left fill rule) ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-edge", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(false);

		// Two screen tris sharing the diagonal from (16,16) to (48,48).
		const float pair[] = {
			16.0f, 16.0f, 0.0f,
			48.0f, 16.0f, 0.0f,
			48.0f, 48.0f, 0.0f,
			16.0f, 16.0f, 0.0f,
			48.0f, 48.0f, 0.0f,
			16.0f, 48.0f, 0.0f};

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.TrisScreen(pair, 2U, hyperlite::PackColor({200, 200, 200, 255}));
		engine.EndFrame();
		engine.Present();

		// Sample along the shared diagonal and nearby interior — no black holes.
		// Use pixel centers that lie strictly inside the quad (avoid AABB corners).
		bool hole = false;
		for (int t = 1; t <= 14; ++t) {
			const int x = 18 + t * 2;
			const int y = 18 + t * 2;
			if (!PixelRgbEquals(ReadPixel(engine, x, y), 200, 200, 200)) {
				hole = true;
			}
		}
		ok = Expect(!hole, "shared edge: no holes along diagonal") && ok;
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 40, 20), 200, 200, 200), "shared edge: upper tri filled") && ok;
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 20, 40), 200, 200, 200), "shared edge: lower tri filled") && ok;
		// Pixel on the geometric diagonal (center on the shared edge) must be owned by exactly one tri.
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 200, 200, 200), "shared edge: diagonal pixel covered once") && ok;
	}

	// --- Identity world ortho: TickTris3d paints known center ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "tri-world", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		const float verts[] = {
			-0.5f, -0.5f, 0.0f,
			0.5f, -0.5f, 0.0f,
			0.0f, 0.5f, 0.0f};
		engine.TickTris3d(
			hyperlite::PackColor({5, 5, 5, 255}),
			verts,
			1U,
			hyperlite::PackColor({0, 128, 255, 255}));
		ok = Expect(PixelRgbEquals(ReadPixel(engine, 32, 32), 0, 128, 255), "world: center of front tri") && ok;
	}

	if (!ok) {
		std::cerr << "depth_tri_tests FAILED\n";
		return 1;
	}
	std::cout << "depth_tri_tests=ok\n";
	return 0;
}
