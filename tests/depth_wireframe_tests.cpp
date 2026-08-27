#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "engine/engine.hpp"
#include "engine/types.hpp"

namespace {

/**
 * Assert helper for deterministic 3D render tests.
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
 * Identity column-major 4x4 (world = clip). Useful for NDC-space tests.
 */
void FillIdentity(float* m) {
	for (int i = 0; i < 16; ++i) {
		m[i] = 0.0f;
	}
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/**
 * Ortho-ish matrix that maps world x,y,z in [-1,1] to clip with w=1 (identity for unit cube).
 *
 * Documented for tests: with this (or identity) matrix, world point (x,y,z) becomes
 * NDC (x,y,z) when |x|,|y|,|z| <= 1. Viewport: NDC (0,0) → pixel center;
 * NDC (+1,0) → right edge; NDC y flips so +Y is up in world, down in pixels.
 */
void FillOrthoIdentity(float* m) {
	FillIdentity(m);
}

} // namespace

/**
 * Unit tests for depth-tested 3D wireframe (headless CPU).
 */
int main() {
	bool ok = true;

	// --- Identity view-proj: line from (0,0,0) to (1,0,0) paints known pixels ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "3d-id", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillOrthoIdentity(vp);
		engine.SetViewProj(vp);

		// World (0,0,0) → NDC (0,0,0) → pixel (~32, ~32)
		// World (1,0,0) → NDC (1,0,0) → pixel (~64, ~32) clamped to edge
		const float segs[] = {
			0.0f, 0.0f, 0.0f,
			1.0f, 0.0f, 0.0f};
		engine.TickLines3d(
			hyperlite::PackColor({0, 0, 0, 255}),
			segs,
			1U,
			hyperlite::PackColor({255, 0, 0, 255}),
			1);

		const hyperlite::Color mid = ReadPixel(engine, 32, 32);
		const hyperlite::Color along = ReadPixel(engine, 48, 32);
		ok = Expect(PixelRgbEquals(mid, 255, 0, 0), "identity: center pixel on line") && ok;
		ok = Expect(PixelRgbEquals(along, 255, 0, 0), "identity: mid-right pixel on line") && ok;
		ok = Expect(engine.DepthEnabled(), "depth stays enabled") && ok;
	}

	// --- Near clip: one endpoint behind camera must not explode; visible part draws ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "3d-near", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		// Front point off-center; back point behind near (z < -w). After clip the
		// visible stub must still span distinct pixels (not a zero-length projection).
		const float segs[] = {
			0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, -2.0f};
		engine.TickLines3d(
			hyperlite::PackColor({10, 10, 10, 255}),
			segs,
			1U,
			hyperlite::PackColor({0, 255, 0, 255}),
			1);

		// Expected clipped B ≈ (0.25, 0, -1) → pixels near x≈40 and x≈48 at y=32.
		const hyperlite::Color a = ReadPixel(engine, 48, 32);
		const hyperlite::Color b = ReadPixel(engine, 40, 32);
		ok = Expect(PixelRgbEquals(a, 0, 255, 0) || PixelRgbEquals(b, 0, 255, 0),
			"near-clip: visible stub still drawn") && ok;
		// Must not leave garbage from behind-camera projection.
		bool any_non_clear_or_green = false;
		const std::uint8_t* data = engine.FramebufferPtr();
		for (int y = 0; y < 64; ++y) {
			for (int x = 0; x < 64; ++x) {
				const std::size_t i = static_cast<std::size_t>((y * 64 + x) * 4);
				const std::uint8_t r = data[i];
				const std::uint8_t g = data[i + 1U];
				const std::uint8_t b = data[i + 2U];
				const bool clear = (r == 10 && g == 10 && b == 10);
				const bool green = (r == 0 && g == 255 && b == 0);
				if (!clear && !green) {
					any_non_clear_or_green = true;
				}
			}
		}
		ok = Expect(!any_non_clear_or_green, "near-clip: no garbage pixels") && ok;
	}

	// --- Depth: two crossing segments; nearer wins at intersection ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "3d-depth", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillOrthoIdentity(vp);
		engine.SetViewProj(vp);

		// Horizontal nearer (z_ndc = -0.5 → window depth 0.25) vs vertical farther (z=0.5 → 0.75).
		// Both cross at NDC origin → pixel (32,32).
		const float near_h[] = {
			-0.5f, 0.0f, -0.5f,
			0.5f, 0.0f, -0.5f};
		const float far_v[] = {
			0.0f, -0.5f, 0.5f,
			0.0f, 0.5f, 0.5f};

		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.Lines3d(far_v, 1U, hyperlite::PackColor({0, 0, 255, 255}), 1);
		engine.Lines3d(near_h, 1U, hyperlite::PackColor({255, 255, 0, 255}), 1);
		engine.EndFrame();
		engine.Present();

		const hyperlite::Color cross = ReadPixel(engine, 32, 32);
		ok = Expect(PixelRgbEquals(cross, 255, 255, 0), "depth: nearer (yellow) wins at cross") && ok;
		const float d = engine.DepthAt(32, 32);
		ok = Expect(d < 0.5f, "depth: window depth nearer than mid") && ok;
	}

	// --- enable_depth(False): 2D TickLines still works; depth reads as far ---
	{
		hyperlite::Engine engine(32, 32, hyperlite::BackendKind::kCpu, "3d-off", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		engine.EnableDepth(false);
		ok = Expect(!engine.DepthEnabled(), "depth disabled") && ok;

		std::vector<std::int32_t> segs = {0, 0, 31, 31};
		engine.TickLines(
			hyperlite::PackColor({5, 5, 5, 255}),
			segs.data(),
			1U,
			hyperlite::PackColor({200, 100, 50, 255}),
			1);
		const hyperlite::Color p = ReadPixel(engine, 16, 16);
		ok = Expect(PixelRgbEquals(p, 200, 100, 50), "2D path intact with depth off") && ok;
		ok = Expect(std::fabs(engine.DepthAt(16, 16) - 1.0f) < 1e-6f, "depth read is 1.0 when off") && ok;
	}

	// --- Mixed frame: 3D then 2D HUD ignores depth ---
	{
		hyperlite::Engine engine(64, 64, hyperlite::BackendKind::kCpu, "3d-hud", hyperlite::PresentMode::kHeadless);
		engine.SetVsync(false);
		engine.EnableDepth(true);
		float vp[16];
		FillIdentity(vp);
		engine.SetViewProj(vp);

		const float world[] = {
			-0.25f, -0.25f, 0.0f,
			0.25f, 0.25f, 0.0f};
		engine.BeginFrame();
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kClear, 0, 0, 0, 0, hyperlite::PackColor({0, 0, 0, 255})));
		engine.Lines3d(world, 1U, hyperlite::PackColor({255, 0, 0, 255}), 1);
		engine.PushCommand(hyperlite::MakeDrawCommand(
			hyperlite::CommandType::kRectFill, 0, 0, 8, 8, hyperlite::PackColor({0, 255, 0, 255})));
		engine.EndFrame();
		engine.Present();

		ok = Expect(PixelRgbEquals(ReadPixel(engine, 2, 2), 0, 255, 0), "HUD rect draws over 3D (no depth)") && ok;
	}

	if (!ok) {
		std::cerr << "depth_wireframe_tests FAILED\n";
		return 1;
	}
	std::cout << "depth_wireframe_tests=ok\n";
	return 0;
}
