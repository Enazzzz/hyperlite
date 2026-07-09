#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/blit_batch.hpp"
#include "engine/types.hpp"

namespace hyperlite {

/**
 * Enumerates all explicit draw operations the user can submit.
 */
enum class CommandType : std::uint8_t {
	kClear,
	kPutPixel,
	kLine,
	kRectFill,
	kRectOutline,
	kUploadFrame,
	kBlit,
	kDrawSprite
};

/**
 * Single POD draw command to keep dispatch branch-light.
 */
struct DrawCommand {
	CommandType type = CommandType::kPutPixel;
	std::uint8_t line_width = 1;
	std::uint16_t reserved = 0;
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	std::uint32_t packed_color = 0xFF000000U;
};

/**
 * Build a draw command with explicit fields (avoids brittle brace-init order).
 */
inline DrawCommand MakeDrawCommand(
	const CommandType type,
	const int x0,
	const int y0,
	const int x1,
	const int y1,
	const std::uint32_t packed_color) {
	DrawCommand command{};
	command.type = type;
	command.x0 = x0;
	command.y0 = y0;
	command.x1 = x1;
	command.y1 = y1;
	command.packed_color = packed_color;
	return command;
}

/**
 * Flat command container that reuses allocation across frames.
 */
class CommandBuffer {
public:
	/**
	 * Prepare queue for new frame without releasing capacity.
	 */
	void Reset() {
		commands_.clear();
		blit_batch_.Reset();
		upload_frame_.clear();
		has_upload_frame_ = false;
		lines_already_sorted_ = false;
	}

	/**
	 * Reserve command capacity to avoid frame allocations.
	 */
	void Reserve(const std::size_t capacity) {
		commands_.reserve(capacity);
	}

	/**
	 * Submit a command for execution.
	 */
	void Push(const DrawCommand& command) {
		lines_already_sorted_ = false;
		commands_.push_back(command);
	}

	/**
	 * Submit a contiguous range of commands in one call.
	 */
	void PushRange(const DrawCommand* commands, const std::size_t count) {
		if (commands == nullptr || count == 0) {
			return;
		}
		lines_already_sorted_ = false;
		commands_.insert(commands_.end(), commands, commands + count);
	}

	/**
	 * Append many line commands from packed int32 tuples [x0,y0,x1,y1,...].
	 */
	void PushLinesBulk(
		const std::int32_t* segments,
		const std::size_t line_count,
		const std::uint32_t packed_color,
		const std::uint8_t line_width = 1U) {
		if (segments == nullptr || line_count == 0U) {
			return;
		}
		const std::size_t old_size = commands_.size();
		commands_.resize(old_size + line_count);
		for (std::size_t line = 0U; line < line_count; ++line) {
			const std::size_t base = line * 4U;
			DrawCommand& command = commands_[old_size + line];
			command.type = CommandType::kLine;
			command.line_width = line_width;
			command.reserved = 0U;
			command.x0 = segments[base + 0U];
			command.y0 = segments[base + 1U];
			command.x1 = segments[base + 2U];
			command.y1 = segments[base + 3U];
			command.packed_color = packed_color;
		}
		lines_already_sorted_ = true;
	}

	/**
	 * Append many line commands with per-segment packed colors.
	 */
	void PushLinesBulkColored(
		const std::int32_t* segments,
		const std::uint32_t* colors,
		const std::size_t line_count,
		const std::uint8_t line_width = 1U) {
		if (segments == nullptr || colors == nullptr || line_count == 0U) {
			return;
		}
		const std::size_t old_size = commands_.size();
		commands_.resize(old_size + line_count);
		for (std::size_t line = 0U; line < line_count; ++line) {
			const std::size_t base = line * 4U;
			DrawCommand& command = commands_[old_size + line];
			command.type = CommandType::kLine;
			command.line_width = line_width;
			command.reserved = 0U;
			command.x0 = segments[base + 0U];
			command.y0 = segments[base + 1U];
			command.x1 = segments[base + 2U];
			command.y1 = segments[base + 3U];
			command.packed_color = colors[line];
		}
		lines_already_sorted_ = false;
	}

	/**
	 * Whether contiguous line runs were queued as one homogeneous bulk (skip sort).
	 */
	bool LinesAlreadySorted() const {
		return lines_already_sorted_;
	}

	/**
	 * Append another batch's blit records and inline payload.
	 */
	void AppendBlitBatch(const BlitBatch& batch) {
		(void)blit_batch_.AppendFrom(batch);
	}

	/**
	 * Stable-sort contiguous blit/sprite runs by material when count exceeds threshold.
	 */
	void SortBlitRunsByMaterial(const std::size_t threshold = 256);

	/**
	 * Stable-sort contiguous line runs by packed color + width when count exceeds threshold.
	 */
	void SortLineRunsByColor(const std::size_t threshold = 64);

	/**
	 * Stage a full-frame RGBA8 upload executed at the matching kUploadFrame command.
	 */
	void StageUploadFrame(const std::uint8_t* src, const std::size_t bytes) {
		upload_frame_.assign(src, src + bytes);
		has_upload_frame_ = true;
	}

	/**
	 * Whether a staged upload is pending for this frame.
	 */
	bool HasUploadFrame() const { return has_upload_frame_; }

	/**
	 * Staged upload bytes for kUploadFrame execution.
	 */
	const std::uint8_t* UploadFrameData() const { return upload_frame_.data(); }

	/**
	 * Staged upload byte count.
	 */
	std::size_t UploadFrameSize() const { return upload_frame_.size(); }

	/**
	 * Queue inline blit payload and return the record index.
	 */
	std::uint32_t PushInlineBlit(
		const std::uint8_t* src,
		const std::size_t bytes,
		const int dst_x,
		const int dst_y,
		const int width,
		const int height) {
		return blit_batch_.PushInline(src, bytes, dst_x, dst_y, width, height);
	}

	/**
	 * Queue atlas sprite draw and return the record index.
	 */
	std::uint32_t PushAtlasBlit(
		const std::uint32_t atlas_id,
		const int src_x,
		const int src_y,
		const int width,
		const int height,
		const int dst_x,
		const int dst_y) {
		return blit_batch_.PushAtlas(atlas_id, src_x, src_y, width, height, dst_x, dst_y);
	}

	/**
	 * Immutable command view used by backends.
	 */
	const std::vector<DrawCommand>& Commands() const {
		return commands_;
	}

	/**
	 * Pointer access for branch-light iteration.
	 */
	const DrawCommand* Data() const {
		return commands_.data();
	}

	/**
	 * Command count in current frame.
	 */
	std::size_t Size() const {
		return commands_.size();
	}

	/**
	 * Per-frame deferred blit batch.
	 */
	const BlitBatch& Blits() const {
		return blit_batch_;
	}

private:
	std::vector<DrawCommand> commands_{};
	BlitBatch blit_batch_{};
	std::vector<std::uint8_t> upload_frame_{};
	bool has_upload_frame_ = false;
	bool lines_already_sorted_ = false;
};

} // namespace hyperlite
