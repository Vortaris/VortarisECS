#include "binary_buffer.h"

#include <cstring>

#include <godot_cpp/variant/char_string.hpp>

namespace vortaris {

void BinaryBuffer::write_u8(uint8_t v) {
	data_.push_back(v);
	++pos_;
}

void BinaryBuffer::write_u16(uint16_t v) {
	data_.push_back(static_cast<uint8_t>(v & 0xFF));
	data_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	pos_ += 2;
}

void BinaryBuffer::write_u32(uint32_t v) {
	data_.push_back(static_cast<uint8_t>(v & 0xFF));
	data_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
	data_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
	data_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
	pos_ += 4;
}

void BinaryBuffer::write_u64(uint64_t v) {
	for (int i = 0; i < 8; ++i) {
		data_.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
	}
	pos_ += 8;
}

void BinaryBuffer::write_str(const godot::String &p_s) {
	godot::CharString utf8 = p_s.utf8();
	write_u32(static_cast<uint32_t>(utf8.length()));
	write_bytes(utf8.ptr(), static_cast<size_t>(utf8.length()));
}

void BinaryBuffer::write_bytes(const void *p_src, size_t p_n) {
	const uint8_t *src = static_cast<const uint8_t *>(p_src);
	data_.insert(data_.end(), src, src + p_n);
	pos_ += p_n;
}

bool BinaryBuffer::read_u8(uint8_t &r_v) {
	if (pos_ + 1 > data_.size()) {
		return false;
	}
	r_v = data_[pos_];
	++pos_;
	return true;
}

bool BinaryBuffer::read_u16(uint16_t &r_v) {
	if (pos_ + 2 > data_.size()) {
		return false;
	}
	r_v = static_cast<uint16_t>(data_[pos_]) | static_cast<uint16_t>(data_[pos_ + 1]) << 8;
	pos_ += 2;
	return true;
}

bool BinaryBuffer::read_u32(uint32_t &r_v) {
	if (pos_ + 4 > data_.size()) {
		return false;
	}
	r_v = static_cast<uint32_t>(data_[pos_]) |
			static_cast<uint32_t>(data_[pos_ + 1]) << 8 |
			static_cast<uint32_t>(data_[pos_ + 2]) << 16 |
			static_cast<uint32_t>(data_[pos_ + 3]) << 24;
	pos_ += 4;
	return true;
}

bool BinaryBuffer::read_u64(uint64_t &r_v) {
	if (pos_ + 8 > data_.size()) {
		return false;
	}
	r_v = 0;
	for (int i = 0; i < 8; ++i) {
		r_v |= static_cast<uint64_t>(data_[pos_ + i]) << (i * 8);
	}
	pos_ += 8;
	return true;
}

bool BinaryBuffer::read_i8(int8_t &r_v) {
	uint8_t v;
	if (!read_u8(v)) {
		return false;
	}
	r_v = static_cast<int8_t>(v);
	return true;
}

bool BinaryBuffer::read_i16(int16_t &r_v) {
	uint16_t v;
	if (!read_u16(v)) {
		return false;
	}
	r_v = static_cast<int16_t>(v);
	return true;
}

bool BinaryBuffer::read_i32(int32_t &r_v) {
	uint32_t v;
	if (!read_u32(v)) {
		return false;
	}
	r_v = static_cast<int32_t>(v);
	return true;
}

bool BinaryBuffer::read_i64(int64_t &r_v) {
	uint64_t v;
	if (!read_u64(v)) {
		return false;
	}
	r_v = static_cast<int64_t>(v);
	return true;
}

bool BinaryBuffer::read_f32(float &r_v) {
	return read_bytes(&r_v, sizeof(float));
}

bool BinaryBuffer::read_f64(double &r_v) {
	return read_bytes(&r_v, sizeof(double));
}

bool BinaryBuffer::read_bool(bool &r_v) {
	uint8_t v;
	if (!read_u8(v)) {
		return false;
	}
	r_v = v != 0;
	return true;
}

bool BinaryBuffer::read_str(godot::String &r_s) {
	uint32_t len;
	if (!read_u32(len)) {
		return false;
	}
	if (pos_ + len > data_.size()) {
		return false;
	}
	r_s = godot::String::utf8(reinterpret_cast<const char *>(data_.data() + pos_), static_cast<int64_t>(len));
	pos_ += len;
	return true;
}

bool BinaryBuffer::read_bytes(void *p_dst, size_t p_n) {
	if (pos_ + p_n > data_.size()) {
		return false;
	}
	if (p_n > 0) {
		std::memcpy(p_dst, data_.data() + pos_, p_n);
	}
	pos_ += p_n;
	return true;
}

void BinaryBuffer::seek(size_t p_pos) {
	pos_ = p_pos < data_.size() ? p_pos : data_.size();
}

void BinaryBuffer::overwrite_u32(size_t p_pos, uint32_t p_value) {
	if (p_pos + 4 > data_.size()) {
		return;
	}
	data_[p_pos] = static_cast<uint8_t>(p_value & 0xFF);
	data_[p_pos + 1] = static_cast<uint8_t>((p_value >> 8) & 0xFF);
	data_[p_pos + 2] = static_cast<uint8_t>((p_value >> 16) & 0xFF);
	data_[p_pos + 3] = static_cast<uint8_t>((p_value >> 24) & 0xFF);
}

godot::PackedByteArray BinaryBuffer::to_packed() const {
	godot::PackedByteArray arr;
	arr.resize(static_cast<int32_t>(data_.size()));
	if (!data_.empty()) {
		std::memcpy(arr.ptrw(), data_.data(), data_.size());
	}
	return arr;
}

void BinaryBuffer::from_packed(const godot::PackedByteArray &p_arr) {
	clear();
	const uint8_t *src = p_arr.ptr();
	data_.assign(src, src + static_cast<size_t>(p_arr.size()));
}

} // namespace vortaris
