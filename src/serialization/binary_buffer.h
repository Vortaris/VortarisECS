#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace vortaris {

// Deterministic binary serializer.
//
// Hard rules:
//   * little-endian, fixed-width primitives (explicit byte encoding, so the
//     output is identical regardless of host endianness);
//   * no pointers, no host-dependent sizes;
//   * all read_* return false on overflow instead of crashing.
class BinaryBuffer {
public:
	void write_u8(uint8_t v);
	void write_i8(int8_t v) { write_u8(static_cast<uint8_t>(v)); }
	void write_u16(uint16_t v);
	void write_i16(int16_t v) { write_u16(static_cast<uint16_t>(v)); }
	void write_u32(uint32_t v);
	void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }
	void write_u64(uint64_t v);
	void write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }
	void write_f32(float v) { write_bytes(&v, sizeof(float)); }
	void write_f64(double v) { write_bytes(&v, sizeof(double)); }
	void write_bool(bool v) { write_u8(v ? 1 : 0); }
	void write_str(const godot::String &p_s);
	void write_bytes(const void *p_src, size_t p_n);

	bool read_u8(uint8_t &r_v);
	bool read_i8(int8_t &r_v);
	bool read_u16(uint16_t &r_v);
	bool read_i16(int16_t &r_v);
	bool read_u32(uint32_t &r_v);
	bool read_i32(int32_t &r_v);
	bool read_u64(uint64_t &r_v);
	bool read_i64(int64_t &r_v);
	bool read_f32(float &r_v);
	bool read_f64(double &r_v);
	bool read_bool(bool &r_v);
	bool read_str(godot::String &r_s);
	bool read_bytes(void *p_dst, size_t p_n);

	size_t size() const { return data_.size(); }
	size_t pos() const { return pos_; }
	bool at_end() const { return pos_ >= data_.size(); }
	void seek(size_t p_pos);
	// In-place overwrite of 4 bytes at an absolute offset (used to patch a
	// length header after the payload is written). No-op if out of range.
	void overwrite_u32(size_t p_pos, uint32_t p_value);
	void clear() { data_.clear(); pos_ = 0; }
	const uint8_t *data() const { return data_.data(); }

	godot::PackedByteArray to_packed() const;
	void from_packed(const godot::PackedByteArray &p_arr);

private:
	std::vector<uint8_t> data_;
	size_t pos_ = 0;
};

} // namespace vortaris
