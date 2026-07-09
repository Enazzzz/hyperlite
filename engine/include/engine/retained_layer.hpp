#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/command_buffer.hpp"

namespace hyperlite {

/**
 * Frozen command/blit snapshot replayable without Python re-recording.
 */
class RetainedLayer {
public:
	/**
	 * Capture the current contents of a command buffer.
	 */
	static RetainedLayer Capture(const CommandBuffer& source);

	/**
	 * Append this layer into an active command buffer, remapping blit indices.
	 */
	void ReplayInto(CommandBuffer& destination) const;

	/**
	 * Number of draw commands stored in the layer.
	 */
	std::size_t CommandCount() const { return commands_.size(); }

private:
	std::vector<DrawCommand> commands_{};
	BlitBatch blit_batch_{};
	std::vector<std::uint8_t> upload_frame_{};
};

} // namespace hyperlite
