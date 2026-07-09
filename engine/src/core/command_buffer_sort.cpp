#include "engine/command_buffer.hpp"

#include <algorithm>

namespace hyperlite {

namespace {

/**
 * Return a sort key for blit/sprite commands (atlas id or max for inline).
 */
std::uint32_t BlitMaterialKey(const DrawCommand& command, const BlitBatch& blits) {
	if (command.packed_color >= blits.Records().size()) {
		return 0xFFFFFFFFU;
	}
	const BlitRecord& record = blits.Records()[command.packed_color];
	if (record.kind == BlitRecord::Kind::kInline) {
		return 0xFFFFFFFEU;
	}
	return record.atlas_id;
}

} // namespace

void CommandBuffer::SortBlitRunsByMaterial(const std::size_t threshold) {
	std::size_t index = 0;
	while (index < commands_.size()) {
		if (commands_[index].type != CommandType::kBlit && commands_[index].type != CommandType::kDrawSprite) {
			++index;
			continue;
		}
		const std::size_t run_start = index;
		while (index < commands_.size() &&
			(commands_[index].type == CommandType::kBlit || commands_[index].type == CommandType::kDrawSprite)) {
			++index;
		}
		const std::size_t run_count = index - run_start;
		if (run_count < threshold) {
			continue;
		}
		std::stable_sort(
			commands_.begin() + static_cast<std::ptrdiff_t>(run_start),
			commands_.begin() + static_cast<std::ptrdiff_t>(index),
			[this](const DrawCommand& left, const DrawCommand& right) {
				return BlitMaterialKey(left, blit_batch_) < BlitMaterialKey(right, blit_batch_);
			});
	}
}

void CommandBuffer::SortLineRunsByColor(const std::size_t threshold) {
	std::size_t index = 0;
	while (index < commands_.size()) {
		if (commands_[index].type != CommandType::kLine) {
			++index;
			continue;
		}
		const std::size_t run_start = index;
		while (index < commands_.size() && commands_[index].type == CommandType::kLine) {
			++index;
		}
		const std::size_t run_count = index - run_start;
		if (run_count < threshold) {
			continue;
		}
		const DrawCommand& ref = commands_[run_start];
		bool homogeneous = true;
		for (std::size_t j = run_start + 1U; j < index; ++j) {
			if (commands_[j].packed_color != ref.packed_color || commands_[j].line_width != ref.line_width) {
				homogeneous = false;
				break;
			}
		}
		if (homogeneous) {
			continue;
		}
		std::stable_sort(
			commands_.begin() + static_cast<std::ptrdiff_t>(run_start),
			commands_.begin() + static_cast<std::ptrdiff_t>(index),
			[](const DrawCommand& left, const DrawCommand& right) {
				if (left.packed_color != right.packed_color) {
					return left.packed_color < right.packed_color;
				}
				return left.line_width < right.line_width;
			});
	}
}

} // namespace hyperlite
