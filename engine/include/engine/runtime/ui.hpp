#pragma once

#include "engine/runtime/math.hpp"
#include "engine/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

class Engine;
class BitmapFont;
class InputRuntime;

struct UiRect {
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;
};

enum class UiAnchor : int {
	TopLeft = 0,
	Top = 1,
	TopRight = 2,
	Left = 3,
	Center = 4,
	Right = 5,
	BottomLeft = 6,
	Bottom = 7,
	BottomRight = 8,
};

/**
 * Immediate-mode UI. State lives in C++; IDs are integer hashes, not Python objects.
 *
 * Call Begin/End around widgets each frame. No hidden Engine tick.
 */
class Ui {
public:
	void Begin(const int width, const int height);
	void End();

	void SetAnchor(const UiAnchor a) { anchor_ = a; }
	UiRect Layout(const float w, const float h, const float x, const float y) const;

	void Label(Engine& engine, const BitmapFont& font, const char* text, const UiRect r, const std::uint32_t color);
	void Image(Engine& engine, const int atlas_id, const int src_x, const int src_y, const int src_w, const int src_h, const UiRect r);
	void Panel(Engine& engine, const UiRect r, const std::uint32_t fill, const std::uint32_t outline);

	bool Button(Engine& engine, const BitmapFont& font, const InputRuntime& input, const char* label, const UiRect r);
	bool Slider(Engine& engine, const InputRuntime& input, const UiRect r, float& value, const float vmin, const float vmax);
	bool InputField(Engine& engine, const BitmapFont& font, const InputRuntime& input, char* buffer, const int cap, const UiRect r);

	int Hot() const { return hot_; }
	int Active() const { return active_; }

private:
	int HashId(const char* s, const UiRect r) const;
	bool Hit(const UiRect r, const Int2 p) const;

	int width_ = 0;
	int height_ = 0;
	UiAnchor anchor_ = UiAnchor::TopLeft;
	int hot_ = 0;
	int active_ = 0;
	int focus_ = 0;
	int frame_ = 0;
};

} // namespace hyperlite
