#include "engine/rasterizer.hpp"

#include "engine/cpu_blend.hpp"
#include "engine/cpu_line_raster.hpp"
#include "engine/depth_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <cstddef>
#include <cstdint>

namespace hyperlite::raster {

namespace {

/**
 * Draw a batch of put-pixel commands with one merged dirty rectangle.
 */
static void DrawPutPixelBatch(FrameBuffer& framebuffer, const DrawCommand* commands, const std::size_t count) {
	if (count == 0U) {
		return;
	}

	const int width = framebuffer.Width();
	const int height = framebuffer.Height();
	auto* dst = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const std::uint32_t packed_color = commands[0].packed_color;
	const std::uint32_t sa = packed_color >> 24U;
	const bool opaque = sa == 255U;
	const bool skip = sa == 0U;

	PixelBounds bounds{};

	for (std::size_t i = 0U; i < count; ++i) {
		const int x = commands[i].x0;
		const int y = commands[i].y0;
		if (static_cast<unsigned int>(x) >= static_cast<unsigned int>(width) ||
			static_cast<unsigned int>(y) >= static_cast<unsigned int>(height)) {
			continue;
		}
		if (!skip) {
			std::uint32_t* ptr = dst + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)) + static_cast<std::size_t>(x);
			if (opaque) {
				*ptr = packed_color;
			} else {
				StorePixel(ptr, packed_color);
			}
		}
		bounds.Expand(x, y);
	}

	if (bounds.valid) {
		framebuffer.NoteDirtyRect(bounds.min_x, bounds.min_y, bounds.max_x + 1, bounds.max_y + 1);
	}
}

/**
 * Fill axis-aligned rectangle while clipping to framebuffer bounds.
 */
static void RectFill(FrameBuffer& framebuffer, int x, int y, int w, int h, const std::uint32_t packed_color) {
	if (w <= 0 || h <= 0) {
		return;
	}
	const int x0 = std::max(x, 0);
	const int y0 = std::max(y, 0);
	const int x1 = std::min(x + w, framebuffer.Width());
	const int y1 = std::min(y + h, framebuffer.Height());
	if (x0 >= x1 || y0 >= y1) {
		return;
	}

	auto* ptr = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	const std::size_t stride = static_cast<std::size_t>(framebuffer.Width());
	const std::size_t row_fill_count = static_cast<std::size_t>(x1 - x0);
	const std::uint32_t sa = packed_color >> 24U;
	if (sa == 255U && (y1 - y0) > 1) {
		FillRectOpaque(ptr, framebuffer.Width(), x0, y0, x1, y1, packed_color);
	} else {
		for (int row = y0; row < y1; ++row) {
			FillSpan(ptr + (static_cast<std::size_t>(row) * stride) + static_cast<std::size_t>(x0), row_fill_count, packed_color);
		}
	}
	framebuffer.NoteDirtyRect(x0, y0, x1, y1);
}

/**
 * Draw rectangle border as one batched line run.
 */
static void RectOutline(FrameBuffer& framebuffer, const int x, const int y, const int w, const int h, const std::uint32_t packed_color) {
	if (w <= 0 || h <= 0) {
		return;
	}
	DrawCommand lines[4]{};
	lines[0] = MakeDrawCommand(CommandType::kLine, x, y, x + w - 1, y, packed_color);
	lines[1] = MakeDrawCommand(CommandType::kLine, x, y, x, y + h - 1, packed_color);
	lines[2] = MakeDrawCommand(CommandType::kLine, x + w - 1, y, x + w - 1, y + h - 1, packed_color);
	lines[3] = MakeDrawCommand(CommandType::kLine, x, y + h - 1, x + w - 1, y + h - 1, packed_color);
	RasterLineBatch(framebuffer, lines, 4U, true);
}

/**
 * Blit RGBA8 source into the framebuffer with clipping and alpha compositing.
 */
static void BlitClipped(
	FrameBuffer& framebuffer,
	const std::uint8_t* src,
	const int src_width,
	const int src_height,
	const int src_x0,
	const int src_y0,
	const int dst_x,
	const int dst_y,
	const int copy_w,
	const int copy_h) {
	if (src == nullptr || copy_w <= 0 || copy_h <= 0) {
		return;
	}
	const int sx0 = std::max(0, -dst_x) + src_x0;
	const int sy0 = std::max(0, -dst_y) + src_y0;
	const int dx0 = std::max(0, dst_x);
	const int dy0 = std::max(0, dst_y);
	const int clipped_w = std::min(copy_w - std::max(0, -dst_x), framebuffer.Width() - dx0);
	const int clipped_h = std::min(copy_h - std::max(0, -dst_y), framebuffer.Height() - dy0);
	if (clipped_w <= 0 || clipped_h <= 0) {
		return;
	}
	(void)src_height;

	const std::size_t src_stride = static_cast<std::size_t>(src_width) * 4U;
	const std::size_t dst_stride_px = static_cast<std::size_t>(framebuffer.Width());
	auto* dst_base = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
	for (int row = 0; row < clipped_h; ++row) {
		const auto* src_row = src + (static_cast<std::size_t>(sy0 + row) * src_stride) + (static_cast<std::size_t>(sx0) * 4U);
		auto* dst_row = dst_base + (static_cast<std::size_t>(dy0 + row) * dst_stride_px) + static_cast<std::size_t>(dx0);
		CopyRowRgba8(dst_row, src_row, clipped_w);
	}
	framebuffer.NoteDirtyRect(dx0, dy0, dx0 + clipped_w, dy0 + clipped_h);
}

/**
 * Execute one deferred blit record.
 */
static void ExecuteBlitRecord(
	FrameBuffer& framebuffer,
	const BlitBatch& blits,
	const AtlasStore& atlases,
	const BlitRecord& record) {
	if (record.kind == BlitRecord::Kind::kInline) {
		const std::uint8_t* src = blits.Data() + record.data_offset;
		BlitClipped(framebuffer, src, record.width, record.height, 0, 0, record.dst_x, record.dst_y, record.width, record.height);
		return;
	}
	const AtlasEntry* atlas = atlases.Get(static_cast<int>(record.atlas_id));
	if (atlas == nullptr) {
		return;
	}
	BlitClipped(
		framebuffer,
		atlas->pixels.data(),
		atlas->width,
		atlas->height,
		record.src_x,
		record.src_y,
		record.dst_x,
		record.dst_y,
		record.width,
		record.height);
}

/**
 * Return true when two line commands can batch rasterize together.
 */
static bool LinesBatchable(const DrawCommand& left, const DrawCommand& right) {
	return right.type == CommandType::kLine &&
		left.packed_color == right.packed_color &&
		left.line_width == right.line_width;
}

/**
 * Return true when two put-pixel commands can batch rasterize together.
 */
static bool PutPixelsBatchable(const DrawCommand& left, const DrawCommand& right) {
	return right.type == CommandType::kPutPixel && left.packed_color == right.packed_color;
}

/**
 * Return true when parallel line raster is safe for this batch.
 */
static bool LinesParallelSafe(const DrawCommand* commands, const std::size_t count) {
	if (count == 0U) {
		return false;
	}
	const std::uint32_t packed = commands[0].packed_color;
	const std::uint8_t width = commands[0].line_width;
	for (std::size_t i = 1U; i < count; ++i) {
		if (commands[i].packed_color != packed || commands[i].line_width != width) {
			return false;
		}
	}
	return (packed >> 24U) == 255U;
}

} // namespace

void ExecuteCommandBuffer(
	const CommandBuffer& command_buffer,
	FrameBuffer& framebuffer,
	const AtlasStore& atlases,
	DepthBuffer* depth) {
	framebuffer.ResetDirty();
	const DrawCommand* commands = command_buffer.Data();
	const std::size_t command_count = command_buffer.Size();
	const auto& blit_records = command_buffer.Blits().Records();
	for (std::size_t i = 0U; i < command_count; ++i) {
		const auto& cmd = commands[i];
		const std::uint32_t packed_color = cmd.packed_color;
		switch (cmd.type) {
		case CommandType::kClear:
			{
				auto* ptr = reinterpret_cast<std::uint32_t*>(framebuffer.Data());
				FillSpan(ptr, framebuffer.PixelCount(), packed_color);
				framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
				if (depth != nullptr && depth->Allocated()) {
					depth->Clear(1.0f);
				}
			}
			break;
		case CommandType::kUploadFrame:
			if (command_buffer.UploadFrameSize() == framebuffer.SizeBytes()) {
				std::memcpy(framebuffer.Data(), command_buffer.UploadFrameData(), framebuffer.SizeBytes());
				framebuffer.NoteDirtyRect(0, 0, framebuffer.Width(), framebuffer.Height());
			}
			break;
		case CommandType::kPutPixel:
			{
				std::size_t batch_end = i + 1U;
				while (batch_end < command_count && PutPixelsBatchable(cmd, commands[batch_end])) {
					++batch_end;
				}
				DrawPutPixelBatch(framebuffer, commands + i, batch_end - i);
				i = batch_end - 1U;
			}
			break;
		case CommandType::kLine:
			{
				std::size_t batch_end = i + 1U;
				while (batch_end < command_count && LinesBatchable(cmd, commands[batch_end])) {
					++batch_end;
				}
				const std::size_t batch_count = batch_end - i;
				RasterLineBatch(framebuffer, commands + i, batch_count, LinesParallelSafe(commands + i, batch_count));
				i = batch_end - 1U;
			}
			break;
		case CommandType::kRectFill:
			RectFill(framebuffer, cmd.x0, cmd.y0, cmd.x1, cmd.y1, packed_color);
			break;
		case CommandType::kRectOutline:
			RectOutline(framebuffer, cmd.x0, cmd.y0, cmd.x1, cmd.y1, packed_color);
			break;
		case CommandType::kBlit:
		case CommandType::kDrawSprite:
			{
				std::size_t batch_end = i + 1U;
				while (batch_end < command_count &&
					(commands[batch_end].type == CommandType::kBlit || commands[batch_end].type == CommandType::kDrawSprite)) {
					++batch_end;
				}
				for (std::size_t b = i; b < batch_end; ++b) {
					const std::uint32_t record_index = commands[b].packed_color;
					if (record_index < blit_records.size()) {
						ExecuteBlitRecord(framebuffer, command_buffer.Blits(), atlases, blit_records[record_index]);
					}
				}
				i = batch_end - 1U;
			}
			break;
		}
	}
	framebuffer.FinalizeDirtyTiles();
}

void RasterWireframeSegments(
	FrameBuffer& framebuffer,
	const std::uint32_t clear_color,
	const std::int32_t* segments,
	const std::size_t line_count,
	const std::uint32_t line_color,
	const int line_width) {
	const bool parallel = (line_color >> 24U) == 255U;
	ClearAndRasterLineSegments(framebuffer, clear_color, segments, line_count, line_color, line_width, parallel);
}

} // namespace hyperlite::raster
