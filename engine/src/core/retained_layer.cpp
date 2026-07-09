#include "engine/retained_layer.hpp"

namespace hyperlite {

RetainedLayer RetainedLayer::Capture(const CommandBuffer& source) {
	RetainedLayer layer{};
	layer.commands_ = source.Commands();
	layer.blit_batch_.AppendFrom(source.Blits());
	if (source.HasUploadFrame()) {
		layer.upload_frame_.assign(source.UploadFrameData(), source.UploadFrameData() + source.UploadFrameSize());
	}
	return layer;
}

void RetainedLayer::ReplayInto(CommandBuffer& destination) const {
	const std::uint32_t blit_index_offset = static_cast<std::uint32_t>(destination.Blits().Records().size());
	for (const DrawCommand& command : commands_) {
		DrawCommand replay = command;
		if (command.type == CommandType::kBlit || command.type == CommandType::kDrawSprite) {
			replay.packed_color = command.packed_color + blit_index_offset;
		}
		destination.Push(replay);
	}
	destination.AppendBlitBatch(blit_batch_);
	if (!upload_frame_.empty()) {
		destination.StageUploadFrame(upload_frame_.data(), upload_frame_.size());
	}
}

} // namespace hyperlite
