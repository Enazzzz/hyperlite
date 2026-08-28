#include "engine/runtime/ui.hpp"

#include "engine/command_buffer.hpp"
#include "engine/engine.hpp"
#include "engine/runtime/input_runtime.hpp"
#include "engine/runtime/text.hpp"

#include <algorithm>
#include <cstring>

namespace hyperlite {

void Ui::Begin(const int width, const int height) {
	width_ = width;
	height_ = height;
	hot_ = 0;
	++frame_;
}

void Ui::End() {
	if (active_ != 0 && active_ != hot_) {
		// keep active until mouse up handled by widgets
	}
}

UiRect Ui::Layout(const float w, const float h, const float x, const float y) const {
	float ax = x;
	float ay = y;
	const float sw = static_cast<float>(width_);
	const float sh = static_cast<float>(height_);
	switch (anchor_) {
	case UiAnchor::TopLeft:
		break;
	case UiAnchor::Top:
		ax = sw * 0.5f - w * 0.5f + x;
		break;
	case UiAnchor::TopRight:
		ax = sw - w + x;
		break;
	case UiAnchor::Left:
		ay = sh * 0.5f - h * 0.5f + y;
		break;
	case UiAnchor::Center:
		ax = sw * 0.5f - w * 0.5f + x;
		ay = sh * 0.5f - h * 0.5f + y;
		break;
	case UiAnchor::Right:
		ax = sw - w + x;
		ay = sh * 0.5f - h * 0.5f + y;
		break;
	case UiAnchor::BottomLeft:
		ay = sh - h + y;
		break;
	case UiAnchor::Bottom:
		ax = sw * 0.5f - w * 0.5f + x;
		ay = sh - h + y;
		break;
	case UiAnchor::BottomRight:
		ax = sw - w + x;
		ay = sh - h + y;
		break;
	}
	return {ax, ay, w, h};
}

int Ui::HashId(const char* s, const UiRect r) const {
	std::uint32_t h = 2166136261u;
	if (s != nullptr) {
		for (const char* p = s; *p; ++p) {
			h ^= static_cast<std::uint8_t>(*p);
			h *= 16777619u;
		}
	}
	h ^= static_cast<std::uint32_t>(r.x) * 374761393u;
	h ^= static_cast<std::uint32_t>(r.y) * 668265263u;
	return static_cast<int>(h) | 1;
}

bool Ui::Hit(const UiRect r, const Int2 p) const {
	return static_cast<float>(p.x) >= r.x && static_cast<float>(p.x) < r.x + r.w &&
		static_cast<float>(p.y) >= r.y && static_cast<float>(p.y) < r.y + r.h;
}

void Ui::Label(Engine& engine, const BitmapFont& font, const char* text, const UiRect r, const std::uint32_t color) {
	font.Draw(engine, text != nullptr ? text : "", static_cast<int>(r.x), static_cast<int>(r.y), color, 1.0f, TextAlign::Left);
}

void Ui::Image(
	Engine& engine,
	const int atlas_id,
	const int src_x,
	const int src_y,
	const int src_w,
	const int src_h,
	const UiRect r) {
	engine.DrawSprite(atlas_id, src_x, src_y, src_w, src_h, static_cast<int>(r.x), static_cast<int>(r.y));
}

void Ui::Panel(Engine& engine, const UiRect r, const std::uint32_t fill, const std::uint32_t outline) {
	engine.PushCommand(MakeDrawCommand(
		CommandType::kRectFill, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w), static_cast<int>(r.h), fill));
	engine.PushCommand(MakeDrawCommand(
		CommandType::kRectOutline, static_cast<int>(r.x), static_cast<int>(r.y), static_cast<int>(r.w), static_cast<int>(r.h), outline));
}

bool Ui::Button(
	Engine& engine,
	const BitmapFont& font,
	const InputRuntime& input,
	const char* label,
	const UiRect r) {
	const int id = HashId(label, r);
	const bool over = Hit(r, input.MousePos());
	if (over) {
		hot_ = id;
	}
	std::uint32_t fill = 0xFF3A3F4Au;
	if (over) {
		fill = 0xFF5A6274u;
	}
	if (over && input.MouseDown(0)) {
		active_ = id;
		fill = 0xFF2A3040u;
	}
	Panel(engine, r, fill, 0xFFD0D6E0u);
	font.Draw(
		engine,
		label != nullptr ? label : "",
		static_cast<int>(r.x + 8.0f),
		static_cast<int>(r.y + r.h * 0.25f),
		0xFFFFFFFFu,
		1.0f,
		TextAlign::Left);
	const bool clicked = over && active_ == id && input.MouseReleased(0);
	if (input.MouseReleased(0) && active_ == id) {
		active_ = 0;
	}
	return clicked;
}

bool Ui::Slider(Engine& engine, const InputRuntime& input, const UiRect r, float& value, const float vmin, const float vmax) {
	const int id = HashId("slider", r);
	const bool over = Hit(r, input.MousePos());
	if (over) {
		hot_ = id;
	}
	Panel(engine, r, 0xFF2A2E38u, 0xFF8892A0u);
	if ((over && input.MouseDown(0)) || active_ == id) {
		active_ = id;
		const float t = Clamp((static_cast<float>(input.MousePos().x) - r.x) / std::max(r.w, 1.0f), 0.0f, 1.0f);
		value = vmin + (vmax - vmin) * t;
	}
	if (input.MouseReleased(0) && active_ == id) {
		active_ = 0;
	}
	const float t = (value - vmin) / std::max(vmax - vmin, 1.0e-6f);
	const int kx = static_cast<int>(r.x + t * r.w) - 4;
	engine.PushCommand(MakeDrawCommand(
		CommandType::kRectFill, kx, static_cast<int>(r.y) - 2, 8, static_cast<int>(r.h) + 4, 0xFFE8EEF8u));
	return over;
}

bool Ui::InputField(
	Engine& engine,
	const BitmapFont& font,
	const InputRuntime& input,
	char* buffer,
	const int cap,
	const UiRect r) {
	if (buffer == nullptr || cap <= 1) {
		return false;
	}
	const int id = HashId("field", r);
	const bool over = Hit(r, input.MousePos());
	if (over && input.MousePressed(0)) {
		focus_ = id;
	}
	Panel(engine, r, focus_ == id ? 0xFF1E2430u : 0xFF161A22u, 0xFFA0A8B4u);
	font.Draw(engine, buffer, static_cast<int>(r.x + 4.0f), static_cast<int>(r.y + 4.0f), 0xFFFFFFFFu, 1.0f, TextAlign::Left);
	if (focus_ != id) {
		return false;
	}
	const int len = static_cast<int>(std::strlen(buffer));
	if (input.KeyPressed(0x08) && len > 0) { // Backspace
		buffer[len - 1] = '\0';
		return true;
	}
	for (int vk = 0x20; vk <= 0x5A; ++vk) {
		if (input.KeyPressed(vk) && len + 1 < cap) {
			buffer[len] = static_cast<char>(vk);
			buffer[len + 1] = '\0';
			return true;
		}
	}
	return false;
}

} // namespace hyperlite
