#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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
	kRectOutline
};

/**
 * Single POD draw command to keep dispatch branch-light.
 */
struct DrawCommand {
	CommandType type = CommandType::kPutPixel;
	int x0 = 0;
	int y0 = 0;
	int x1 = 0;
	int y1 = 0;
	std::uint32_t packed_color = 0xFF000000U;
};

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
		commands_.push_back(command);
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

private:
	std::vector<DrawCommand> commands_{};
};

} // namespace hyperlite
