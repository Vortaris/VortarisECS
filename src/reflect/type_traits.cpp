#include "type_traits.h"

#include <algorithm>
#include <cstdint>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/char_string.hpp>

namespace vortaris {

namespace {

size_t safe_strnlen(const char *p_s, size_t p_max) {
	size_t n = 0;
	while (n < p_max && p_s[n] != '\0') {
		++n;
	}
	return n;
}

bool scalar_to_variant(FieldType p_type, const void *p_src, godot::Variant &r_out) {
	switch (p_type) {
		case FieldType::Bool:
			r_out = *static_cast<const bool *>(p_src);
			return true;
		case FieldType::I8:
			r_out = static_cast<int64_t>(*static_cast<const int8_t *>(p_src));
			return true;
		case FieldType::I16:
			r_out = static_cast<int64_t>(*static_cast<const int16_t *>(p_src));
			return true;
		case FieldType::I32:
			r_out = static_cast<int64_t>(*static_cast<const int32_t *>(p_src));
			return true;
		case FieldType::I64:
			r_out = *static_cast<const int64_t *>(p_src);
			return true;
		case FieldType::U8:
			r_out = static_cast<int64_t>(*static_cast<const uint8_t *>(p_src));
			return true;
		case FieldType::U16:
			r_out = static_cast<int64_t>(*static_cast<const uint16_t *>(p_src));
			return true;
		case FieldType::U32:
			r_out = static_cast<int64_t>(*static_cast<const uint32_t *>(p_src));
			return true;
		case FieldType::U64:
			r_out = static_cast<int64_t>(*static_cast<const uint64_t *>(p_src));
			return true;
		case FieldType::F32:
			r_out = static_cast<double>(*static_cast<const float *>(p_src));
			return true;
		case FieldType::F64:
			r_out = *static_cast<const double *>(p_src);
			return true;
		case FieldType::Vector2:
			r_out = *static_cast<const godot::Vector2 *>(p_src);
			return true;
		case FieldType::Vector2i:
			r_out = *static_cast<const godot::Vector2i *>(p_src);
			return true;
		case FieldType::Vector3:
			r_out = *static_cast<const godot::Vector3 *>(p_src);
			return true;
		case FieldType::Vector3i:
			r_out = *static_cast<const godot::Vector3i *>(p_src);
			return true;
		case FieldType::Vector4:
			r_out = *static_cast<const godot::Vector4 *>(p_src);
			return true;
		case FieldType::Vector4i:
			r_out = *static_cast<const godot::Vector4i *>(p_src);
			return true;
		case FieldType::Color:
			r_out = *static_cast<const godot::Color *>(p_src);
			return true;
		case FieldType::Quaternion:
			r_out = *static_cast<const godot::Quaternion *>(p_src);
			return true;
		case FieldType::Basis:
			r_out = *static_cast<const godot::Basis *>(p_src);
			return true;
		case FieldType::Transform2D:
			r_out = *static_cast<const godot::Transform2D *>(p_src);
			return true;
		case FieldType::Transform3D:
			r_out = *static_cast<const godot::Transform3D *>(p_src);
			return true;
		case FieldType::AABB:
			r_out = *static_cast<const godot::AABB *>(p_src);
			return true;
		case FieldType::Rect2:
			r_out = *static_cast<const godot::Rect2 *>(p_src);
			return true;
		case FieldType::Plane:
			r_out = *static_cast<const godot::Plane *>(p_src);
			return true;
		default:
			return false;
	}
}

bool scalar_from_variant(FieldType p_type, void *p_dst, const godot::Variant &p_in) {
	switch (p_type) {
		case FieldType::Bool: {
			bool v = p_in;
			std::memcpy(p_dst, &v, sizeof(bool));
			return true;
		}
		case FieldType::I8: {
			int8_t v = static_cast<int8_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(int8_t));
			return true;
		}
		case FieldType::I16: {
			int16_t v = static_cast<int16_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(int16_t));
			return true;
		}
		case FieldType::I32: {
			int32_t v = static_cast<int32_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(int32_t));
			return true;
		}
		case FieldType::I64: {
			int64_t v = static_cast<int64_t>(p_in);
			std::memcpy(p_dst, &v, sizeof(int64_t));
			return true;
		}
		case FieldType::U8: {
			uint8_t v = static_cast<uint8_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(uint8_t));
			return true;
		}
		case FieldType::U16: {
			uint16_t v = static_cast<uint16_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(uint16_t));
			return true;
		}
		case FieldType::U32: {
			uint32_t v = static_cast<uint32_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(uint32_t));
			return true;
		}
		case FieldType::U64: {
			uint64_t v = static_cast<uint64_t>(static_cast<int64_t>(p_in));
			std::memcpy(p_dst, &v, sizeof(uint64_t));
			return true;
		}
		case FieldType::F32: {
			float v = static_cast<float>(static_cast<double>(p_in));
			std::memcpy(p_dst, &v, sizeof(float));
			return true;
		}
		case FieldType::F64: {
			double v = static_cast<double>(p_in);
			std::memcpy(p_dst, &v, sizeof(double));
			return true;
		}
		case FieldType::Vector2: {
			godot::Vector2 v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector2));
			return true;
		}
		case FieldType::Vector2i: {
			godot::Vector2i v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector2i));
			return true;
		}
		case FieldType::Vector3: {
			godot::Vector3 v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector3));
			return true;
		}
		case FieldType::Vector3i: {
			godot::Vector3i v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector3i));
			return true;
		}
		case FieldType::Vector4: {
			godot::Vector4 v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector4));
			return true;
		}
		case FieldType::Vector4i: {
			godot::Vector4i v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Vector4i));
			return true;
		}
		case FieldType::Color: {
			godot::Color v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Color));
			return true;
		}
		case FieldType::Quaternion: {
			godot::Quaternion v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Quaternion));
			return true;
		}
		case FieldType::Basis: {
			godot::Basis v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Basis));
			return true;
		}
		case FieldType::Transform2D: {
			godot::Transform2D v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Transform2D));
			return true;
		}
		case FieldType::Transform3D: {
			godot::Transform3D v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Transform3D));
			return true;
		}
		case FieldType::AABB: {
			godot::AABB v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::AABB));
			return true;
		}
		case FieldType::Rect2: {
			godot::Rect2 v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Rect2));
			return true;
		}
		case FieldType::Plane: {
			godot::Plane v = p_in;
			std::memcpy(p_dst, &v, sizeof(godot::Plane));
			return true;
		}
		default:
			return false;
	}
}

} // namespace

bool element_to_variant(FieldType p_type, const void *p_src, godot::Variant &r_out) {
	return scalar_to_variant(p_type, p_src, r_out);
}

bool element_from_variant(FieldType p_type, void *p_dst, const godot::Variant &p_in) {
	return scalar_from_variant(p_type, p_dst, p_in);
}

bool field_to_variant(const FieldDescriptor &p_fd, const void *p_src, godot::Variant &r_out) {
	if (p_fd.type == FieldType::StringFixed) {
		const char *buf = static_cast<const char *>(p_src);
		size_t len = safe_strnlen(buf, p_fd.count);
		r_out = godot::String::utf8(buf, static_cast<int64_t>(len));
		return true;
	}
	if (p_fd.type == FieldType::Blob) {
		godot::PackedByteArray arr;
		arr.resize(static_cast<int32_t>(p_fd.count));
		std::memcpy(arr.ptrw(), p_src, p_fd.count);
		r_out = arr;
		return true;
	}

	if (p_fd.count <= 1) {
		return scalar_to_variant(p_fd.type, p_src, r_out);
	}

	// Fixed array: element stride = storage_size / count.
	size_t elem_size = p_fd.storage_size() / p_fd.count;
	godot::Array out;
	out.resize(static_cast<int32_t>(p_fd.count));
	const uint8_t *ptr = static_cast<const uint8_t *>(p_src);
	for (size_t i = 0; i < p_fd.count; ++i) {
		godot::Variant v;
		if (!scalar_to_variant(static_cast<FieldType>(p_fd.element_type), ptr + i * elem_size, v)) {
			return false;
		}
		out[static_cast<int32_t>(i)] = v;
	}
	r_out = out;
	return true;
}

bool field_from_variant(const FieldDescriptor &p_fd, void *p_dst, const godot::Variant &p_in) {
	if (p_fd.type == FieldType::StringFixed) {
		godot::String s = p_in;
		// A zero-length StringFixed buffer holds nothing; store the empty string
		// without writing past the (zero-sized) storage.
		if (p_fd.count == 0) {
			return true;
		}
		size_t max_len = p_fd.count - 1; // keep room for NUL
		godot::CharString utf8 = s.utf8();
		size_t copy = std::min<size_t>(static_cast<size_t>(utf8.length()), max_len);
		// Never cut a multi-byte UTF-8 code point in half. If the byte just past
		// the truncation point is a continuation byte, the code point straddling
		// the boundary is incomplete: back off to its lead byte so the stored
		// string is always valid UTF-8.
		const uint8_t *src = reinterpret_cast<const uint8_t *>(utf8.ptr());
		if (copy < static_cast<size_t>(utf8.length()) && copy > 0 && (src[copy] & 0xC0) == 0x80) {
			size_t p = copy;
			while (p > 0 && (src[p] & 0xC0) == 0x80) {
				--p;
			}
			copy = p;
		}
		if (copy < static_cast<size_t>(utf8.length())) {
			WARN_PRINT("VortarisECS: StringFixed field truncated from " +
					godot::String::num_uint64(static_cast<uint64_t>(utf8.length())) +
					" to " + godot::String::num_uint64(static_cast<uint64_t>(copy)) +
					" bytes (buffer capacity " + godot::String::num_uint64(static_cast<uint64_t>(p_fd.count)) +
					", kept whole UTF-8 characters).");
		}
		std::memcpy(p_dst, src, copy);
		static_cast<char *>(p_dst)[copy] = '\0';
		return true;
	}
	if (p_fd.type == FieldType::Blob) {
		godot::PackedByteArray arr = p_in;
		size_t copy = std::min<size_t>(static_cast<size_t>(arr.size()), p_fd.count);
		std::memcpy(p_dst, arr.ptr(), copy);
		if (copy < p_fd.count) {
			std::memset(static_cast<uint8_t *>(p_dst) + copy, 0, p_fd.count - copy);
		}
		return true;
	}

	if (p_fd.count <= 1) {
		return scalar_from_variant(p_fd.type, p_dst, p_in);
	}

	size_t elem_size = p_fd.storage_size() / p_fd.count;
	godot::Array in = p_in;
	size_t n = std::min<size_t>(static_cast<size_t>(in.size()), p_fd.count);
	uint8_t *ptr = static_cast<uint8_t *>(p_dst);
	for (size_t i = 0; i < n; ++i) {
		if (!scalar_from_variant(static_cast<FieldType>(p_fd.element_type), ptr + i * elem_size, in[static_cast<int32_t>(i)])) {
			return false;
		}
	}
	return true;
}

void component_bytes_to_variant_dict(const ComponentSchema &p_schema, const void *p_src, godot::Dictionary &r_out) {
	for (const auto &fd : p_schema.fields) {
		godot::Variant v;
		if (field_to_variant(fd, static_cast<const uint8_t *>(p_src) + fd.offset, v)) {
			r_out[fd.name] = v;
		}
	}
}

void component_dict_to_bytes(const ComponentSchema &p_schema, void *p_dst, const godot::Dictionary &p_in) {
	// Start from zero so absent fields are zero-initialized.
	std::memset(p_dst, 0, p_schema.size);
	for (const auto &fd : p_schema.fields) {
		if (p_in.has(fd.name)) {
			field_from_variant(fd, static_cast<uint8_t *>(p_dst) + fd.offset, p_in[fd.name]);
		}
	}
}

bool variants_equal(const godot::Variant &p_a, const godot::Variant &p_b) {
	const godot::Variant::Type ta = p_a.get_type();
	const godot::Variant::Type tb = p_b.get_type();
	if (ta == tb) {
		return p_a == p_b;
	}
	// Cross-type numeric coercion (GDScript `==` semantics): INT / FLOAT / BOOL
	// compare numerically, keeping 64-bit integer precision when neither side is
	// a float (only then fall through to double).
	const bool a_num = ta == godot::Variant::INT || ta == godot::Variant::FLOAT || ta == godot::Variant::BOOL;
	const bool b_num = tb == godot::Variant::INT || tb == godot::Variant::FLOAT || tb == godot::Variant::BOOL;
	if (a_num && b_num) {
		if (ta == godot::Variant::FLOAT || tb == godot::Variant::FLOAT) {
			return static_cast<double>(p_a) == static_cast<double>(p_b);
		}
		return static_cast<int64_t>(p_a) == static_cast<int64_t>(p_b);
	}
	return p_a == p_b;
}

} // namespace vortaris
