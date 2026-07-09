#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * Describes one deferred blit or atlas sprite draw.
 */
struct BlitRecord {
	enum class Kind : std::uint8_t {
		kInline,
		kAtlas
	};

	Kind kind = Kind::kInline;
	int dst_x = 0;
	int dst_y = 0;
	int src_x = 0;
	int src_y = 0;
	int width = 0;
	int height = 0;
	std::uint32_t atlas_id = 0;
	std::uint32_t data_offset = 0;
};

/**
 * Per-frame inline blit payload pool and descriptor list.
 */
class BlitBatch {
public:
	/**
	 * Clear per-frame blit state without releasing capacity.
	 */
	void Reset() {
		records_.clear();
		data_.clear();
	}

	/**
	 * Append inline RGBA8 pixels and return the record index.
	 */
	std::uint32_t PushInline(const std::uint8_t* src, const std::size_t bytes, const int dst_x, const int dst_y, const int width, const int height) {
		BlitRecord record{};
		record.kind = BlitRecord::Kind::kInline;
		record.dst_x = dst_x;
		record.dst_y = dst_y;
		record.width = width;
		record.height = height;
		record.data_offset = static_cast<std::uint32_t>(data_.size());
		data_.insert(data_.end(), src, src + bytes);
		records_.push_back(record);
		return static_cast<std::uint32_t>(records_.size() - 1U);
	}

	/**
	 * Append an atlas sub-rect draw and return the record index.
	 */
	std::uint32_t PushAtlas(
		const std::uint32_t atlas_id,
		const int src_x,
		const int src_y,
		const int width,
		const int height,
		const int dst_x,
		const int dst_y) {
		BlitRecord record{};
		record.kind = BlitRecord::Kind::kAtlas;
		record.atlas_id = atlas_id;
		record.src_x = src_x;
		record.src_y = src_y;
		record.width = width;
		record.height = height;
		record.dst_x = dst_x;
		record.dst_y = dst_y;
		records_.push_back(record);
		return static_cast<std::uint32_t>(records_.size() - 1U);
	}

	const std::vector<BlitRecord>& Records() const { return records_; }
	const std::uint8_t* Data() const { return data_.data(); }
	std::size_t DataSize() const { return data_.size(); }

	/**
	 * Append another batch, returning the record index offset for command remapping.
	 */
	std::uint32_t AppendFrom(const BlitBatch& other) {
		const std::uint32_t record_offset = static_cast<std::uint32_t>(records_.size());
		const std::uint32_t data_offset = static_cast<std::uint32_t>(data_.size());
		for (const BlitRecord& record : other.records_) {
			BlitRecord copy = record;
			if (copy.kind == BlitRecord::Kind::kInline) {
				copy.data_offset += data_offset;
			}
			records_.push_back(copy);
		}
		data_.insert(data_.end(), other.data_.begin(), other.data_.end());
		return record_offset;
	}

private:
	std::vector<BlitRecord> records_{};
	std::vector<std::uint8_t> data_{};
};

} // namespace hyperlite
