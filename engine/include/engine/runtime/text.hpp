#pragma once

#include "engine/runtime/math.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

class Engine;

enum class TextAlign : int { Left = 0, Center = 1, Right = 2 };

struct Glyph {
	int code = 0;
	int x = 0;
	int y = 0;
	int w = 8;
	int h = 8;
	int advance = 8;
};

/**
 * Bitmap font: 8×8 CPU glyphs by default (embedded), or an atlas of Glyphs.
 */
class BitmapFont {
public:
	BitmapFont();

	void SetGlyphs(std::vector<Glyph> glyphs, const int atlas_id, const int atlas_w, const int atlas_h);
	int Measure(const char* text, const float scale = 1.0f) const;
	int LineHeight(const float scale = 1.0f) const { return static_cast<int>(8.0f * scale); }

	/**
	 * Draw ASCII text as filled rects (embedded font) or sprites (atlas font).
	 */
	void Draw(
		Engine& engine,
		const char* text,
		const int x,
		const int y,
		const std::uint32_t packed_color,
		const float scale = 1.0f,
		const TextAlign align = TextAlign::Left) const;

private:
	const Glyph* Find(const int code) const;
	std::vector<Glyph> glyphs_{};
	int atlas_id_ = -1;
	int atlas_w_ = 0;
	int atlas_h_ = 0;
	bool embedded_ = true;
};

} // namespace hyperlite
