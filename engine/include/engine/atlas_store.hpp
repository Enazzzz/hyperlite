#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hyperlite {

/**
 * CPU-resident RGBA8 sprite atlas storage keyed by integer handle.
 */
struct AtlasEntry {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> pixels{};
};

/**
 * CPU-resident sprite atlas storage keyed by integer handle.
 */
class AtlasStore {
public:
	/**
	 * Upload RGBA8 atlas pixels and return a stable handle.
	 */
	int Load(const std::uint8_t* src, const std::size_t bytes, const int width, const int height);

	/**
	 * Read one atlas entry; returns nullptr when handle is invalid.
	 */
	const AtlasEntry* Get(const int handle) const;

	/**
	 * Number of loaded atlases.
	 */
	std::size_t Count() const { return entries_.size(); }

private:
	std::vector<AtlasEntry> entries_{};
};

} // namespace hyperlite
