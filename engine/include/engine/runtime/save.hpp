#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hyperlite {

/**
 * Binary writer with a versioned header. Native, no pickle.
 */
class BinaryWriter {
public:
	explicit BinaryWriter(const std::uint32_t version = 1);

	void WriteU8(const std::uint8_t v);
	void WriteU32(const std::uint32_t v);
	void WriteF32(const float v);
	void WriteBytes(const void* data, const std::size_t bytes);
	void WriteString(const char* s);

	const std::vector<std::uint8_t>& Data() const { return data_; }
	bool SaveFile(const char* path) const;

private:
	std::vector<std::uint8_t> data_{};
};

/**
 * Binary reader matching BinaryWriter.
 */
class BinaryReader {
public:
	bool LoadFile(const char* path);
	void LoadBytes(const std::uint8_t* data, const std::size_t bytes);

	std::uint32_t Version() const { return version_; }
	bool Ok() const { return ok_; }

	std::uint8_t ReadU8();
	std::uint32_t ReadU32();
	float ReadF32();
	void ReadBytes(void* data, const std::size_t bytes);
	std::string ReadString();

private:
	std::vector<std::uint8_t> data_{};
	std::size_t cursor_ = 0;
	std::uint32_t version_ = 0;
	bool ok_ = false;
};

} // namespace hyperlite
