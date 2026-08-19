#include "component_serializer.h"

#include <vector>

namespace vortaris {

namespace {

// Per-field write. Scalar fields write their fixed width; Vector/Color/
// Transform/Blob write storage_size raw bytes; StringFixed writes content up
// to the NUL terminator and zero-pads the rest (deterministic bytes even when
// C++ stored a shorter string).
void write_field(const FieldDescriptor &p_fd, const uint8_t *p_ptr, BinaryBuffer &r_buf) {
	switch (p_fd.type) {
		case FieldType::Bool:
			r_buf.write_u8(*p_ptr);
			break;
		case FieldType::I8:
			r_buf.write_i8(*reinterpret_cast<const int8_t *>(p_ptr));
			break;
		case FieldType::U8:
			r_buf.write_u8(*p_ptr);
			break;
		case FieldType::I16:
			r_buf.write_i16(*reinterpret_cast<const int16_t *>(p_ptr));
			break;
		case FieldType::U16:
			r_buf.write_u16(*reinterpret_cast<const uint16_t *>(p_ptr));
			break;
		case FieldType::I32:
			r_buf.write_i32(*reinterpret_cast<const int32_t *>(p_ptr));
			break;
		case FieldType::U32:
			r_buf.write_u32(*reinterpret_cast<const uint32_t *>(p_ptr));
			break;
		case FieldType::I64:
			r_buf.write_i64(*reinterpret_cast<const int64_t *>(p_ptr));
			break;
		case FieldType::U64:
			r_buf.write_u64(*reinterpret_cast<const uint64_t *>(p_ptr));
			break;
		case FieldType::F32:
			r_buf.write_f32(*reinterpret_cast<const float *>(p_ptr));
			break;
		case FieldType::F64:
			r_buf.write_f64(*reinterpret_cast<const double *>(p_ptr));
			break;
		default:
			if (p_fd.type == FieldType::StringFixed) {
				const char *s = reinterpret_cast<const char *>(p_ptr);
				size_t len = 0;
				while (len < p_fd.storage_size() && s[len] != '\0') {
					++len;
				}
				r_buf.write_bytes(p_ptr, len);
				const uint8_t zero = 0;
				for (size_t k = len; k < p_fd.storage_size(); ++k) {
					r_buf.write_bytes(&zero, 1);
				}
			} else {
				r_buf.write_bytes(p_ptr, p_fd.storage_size());
			}
			break;
	}
}

// Per-field read. Returns false when the buffer runs out.
bool read_field(const FieldDescriptor &p_fd, uint8_t *p_ptr, BinaryBuffer &r_buf) {
	switch (p_fd.type) {
		case FieldType::Bool: {
			uint8_t v;
			if (!r_buf.read_u8(v)) {
				return false;
			}
			*p_ptr = v;
			break;
		}
		case FieldType::I8: {
			int8_t v;
			if (!r_buf.read_i8(v)) {
				return false;
			}
			*reinterpret_cast<int8_t *>(p_ptr) = v;
			break;
		}
		case FieldType::U8: {
			uint8_t v;
			if (!r_buf.read_u8(v)) {
				return false;
			}
			*p_ptr = v;
			break;
		}
		case FieldType::I16: {
			int16_t v;
			if (!r_buf.read_i16(v)) {
				return false;
			}
			*reinterpret_cast<int16_t *>(p_ptr) = v;
			break;
		}
		case FieldType::U16: {
			uint16_t v;
			if (!r_buf.read_u16(v)) {
				return false;
			}
			*reinterpret_cast<uint16_t *>(p_ptr) = v;
			break;
		}
		case FieldType::I32: {
			int32_t v;
			if (!r_buf.read_i32(v)) {
				return false;
			}
			*reinterpret_cast<int32_t *>(p_ptr) = v;
			break;
		}
		case FieldType::U32: {
			uint32_t v;
			if (!r_buf.read_u32(v)) {
				return false;
			}
			*reinterpret_cast<uint32_t *>(p_ptr) = v;
			break;
		}
		case FieldType::I64: {
			int64_t v;
			if (!r_buf.read_i64(v)) {
				return false;
			}
			*reinterpret_cast<int64_t *>(p_ptr) = v;
			break;
		}
		case FieldType::U64: {
			uint64_t v;
			if (!r_buf.read_u64(v)) {
				return false;
			}
			*reinterpret_cast<uint64_t *>(p_ptr) = v;
			break;
		}
		case FieldType::F32: {
			float v;
			if (!r_buf.read_f32(v)) {
				return false;
			}
			*reinterpret_cast<float *>(p_ptr) = v;
			break;
		}
		case FieldType::F64: {
			double v;
			if (!r_buf.read_f64(v)) {
				return false;
			}
			*reinterpret_cast<double *>(p_ptr) = v;
			break;
		}
		default:
			if (!r_buf.read_bytes(p_ptr, p_fd.storage_size())) {
				return false;
			}
			break;
	}
	return true;
}

size_t field_size(const FieldDescriptor &p_fd) {
	switch (p_fd.type) {
		case FieldType::Bool:
		case FieldType::I8:
		case FieldType::U8:
			return 1;
		case FieldType::I16:
		case FieldType::U16:
			return 2;
		case FieldType::I32:
		case FieldType::U32:
		case FieldType::F32:
			return 4;
		case FieldType::I64:
		case FieldType::U64:
		case FieldType::F64:
			return 8;
		default:
			// Vector/Color/Transform/StringFixed/Blob: fixed storage_size bytes.
			return p_fd.storage_size();
	}
}

} // namespace

void serialize_component(const ComponentSchema &p_schema, const void *p_inst, BinaryBuffer &r_buf) {
	const uint8_t *base = static_cast<const uint8_t *>(p_inst);
	for (const FieldDescriptor &fd : p_schema.fields) {
		write_field(fd, base + fd.offset, r_buf);
	}
}

size_t serialized_component_size(const ComponentSchema &p_schema) {
	size_t total = 0;
	for (const FieldDescriptor &fd : p_schema.fields) {
		total += field_size(fd);
	}
	return total;
}

bool deserialize_component(const ComponentSchema &p_schema, void *p_inst, BinaryBuffer &r_buf) {
	uint8_t *base = static_cast<uint8_t *>(p_inst);
	for (const FieldDescriptor &fd : p_schema.fields) {
		if (!read_field(fd, base + fd.offset, r_buf)) {
			return false;
		}
	}
	return true;
}

void serialize_component_fields(const ComponentSchema &p_schema, const void *p_inst,
		BinaryBuffer &r_buf, const std::vector<bool> &p_mask) {
	const uint8_t *base = static_cast<const uint8_t *>(p_inst);
	for (size_t i = 0; i < p_schema.fields.size(); ++i) {
		if (i < p_mask.size() && !p_mask[i]) {
			continue;
		}
		write_field(p_schema.fields[i], base + p_schema.fields[i].offset, r_buf);
	}
}

bool deserialize_component_fields(const ComponentSchema &p_schema, void *p_inst,
		BinaryBuffer &r_buf, const std::vector<bool> &p_mask) {
	uint8_t *base = static_cast<uint8_t *>(p_inst);
	for (size_t i = 0; i < p_schema.fields.size(); ++i) {
		if (i < p_mask.size() && !p_mask[i]) {
			continue;
		}
		if (!read_field(p_schema.fields[i], base + p_schema.fields[i].offset, r_buf)) {
			return false;
		}
	}
	return true;
}

size_t serialized_component_fields_size(const ComponentSchema &p_schema, const std::vector<bool> &p_mask) {
	size_t total = 0;
	for (size_t i = 0; i < p_schema.fields.size(); ++i) {
		if (i < p_mask.size() && !p_mask[i]) {
			continue;
		}
		total += field_size(p_schema.fields[i]);
	}
	return total;
}

size_t field_mask_bytes(size_t p_field_count) {
	return (p_field_count + 7) / 8;
}

} // namespace vortaris
