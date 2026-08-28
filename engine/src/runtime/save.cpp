#include "engine/runtime/save.hpp"

#include <cstdio>
#include <cstring>

namespace hyperlite {

BinaryWriter::BinaryWriter(const std::uint32_t version) {
	WriteU32(0x48595052u); // HYPR
	WriteU32(version);
}

void BinaryWriter::WriteU8(const std::uint8_t v) {
	data_.push_back(v);
}

void BinaryWriter::WriteU32(const std::uint32_t v) {
	data_.push_back(static_cast<std::uint8_t>(v));
	data_.push_back(static_cast<std::uint8_t>(v >> 8));
	data_.push_back(static_cast<std::uint8_t>(v >> 16));
	data_.push_back(static_cast<std::uint8_t>(v >> 24));
}

void BinaryWriter::WriteF32(const float v) {
	std::uint32_t bits = 0;
	std::memcpy(&bits, &v, 4);
	WriteU32(bits);
}

void BinaryWriter::WriteBytes(const void* data, const std::size_t bytes) {
	if (data == nullptr || bytes == 0) {
		return;
	}
	const auto* p = static_cast<const std::uint8_t*>(data);
	data_.insert(data_.end(), p, p + bytes);
}

void BinaryWriter::WriteString(const char* s) {
	const std::uint32_t n = s != nullptr ? static_cast<std::uint32_t>(std::strlen(s)) : 0U;
	WriteU32(n);
	if (n > 0) {
		WriteBytes(s, n);
	}
}

bool BinaryWriter::SaveFile(const char* path) const {
	if (path == nullptr) {
		return false;
	}
	FILE* f = std::fopen(path, "wb");
	if (f == nullptr) {
		return false;
	}
	const bool ok = std::fwrite(data_.data(), 1, data_.size(), f) == data_.size();
	std::fclose(f);
	return ok;
}

bool BinaryReader::LoadFile(const char* path) {
	if (path == nullptr) {
		ok_ = false;
		return false;
	}
	FILE* f = std::fopen(path, "rb");
	if (f == nullptr) {
		ok_ = false;
		return false;
	}
	std::fseek(f, 0, SEEK_END);
	const long sz = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (sz < 8) {
		std::fclose(f);
		ok_ = false;
		return false;
	}
	data_.resize(static_cast<std::size_t>(sz));
	const bool ok = std::fread(data_.data(), 1, data_.size(), f) == data_.size();
	std::fclose(f);
	if (!ok) {
		ok_ = false;
		return false;
	}
	LoadBytes(data_.data(), data_.size());
	return ok_;
}

void BinaryReader::LoadBytes(const std::uint8_t* data, const std::size_t bytes) {
	data_.assign(data, data + bytes);
	cursor_ = 0;
	ok_ = true;
	const std::uint32_t magic = ReadU32();
	version_ = ReadU32();
	if (magic != 0x48595052u) {
		ok_ = false;
	}
}

std::uint8_t BinaryReader::ReadU8() {
	if (!ok_ || cursor_ >= data_.size()) {
		ok_ = false;
		return 0;
	}
	return data_[cursor_++];
}

std::uint32_t BinaryReader::ReadU32() {
	const std::uint8_t a = ReadU8();
	const std::uint8_t b = ReadU8();
	const std::uint8_t c = ReadU8();
	const std::uint8_t d = ReadU8();
	return static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) |
		(static_cast<std::uint32_t>(c) << 16) | (static_cast<std::uint32_t>(d) << 24);
}

float BinaryReader::ReadF32() {
	const std::uint32_t bits = ReadU32();
	float v = 0.0f;
	std::memcpy(&v, &bits, 4);
	return v;
}

void BinaryReader::ReadBytes(void* data, const std::size_t bytes) {
	if (!ok_ || cursor_ + bytes > data_.size()) {
		ok_ = false;
		return;
	}
	std::memcpy(data, data_.data() + cursor_, bytes);
	cursor_ += bytes;
}

std::string BinaryReader::ReadString() {
	const std::uint32_t n = ReadU32();
	if (!ok_ || cursor_ + n > data_.size()) {
		ok_ = false;
		return {};
	}
	std::string s(reinterpret_cast<const char*>(data_.data() + cursor_), n);
	cursor_ += n;
	return s;
}

} // namespace hyperlite
