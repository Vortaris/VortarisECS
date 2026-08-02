#include "component_serializer.h"

namespace vortaris {

void serialize_component(const ComponentSchema &p_schema, const void *p_inst, BinaryBuffer &r_buf) {
	const uint8_t *base = static_cast<const uint8_t *>(p_inst);
	for (const FieldDescriptor &fd : p_schema.fields) {
		const uint8_t *ptr = base + fd.offset;
		switch (fd.type) {
			case FieldType::Bool:
				r_buf.write_u8(*ptr);
				break;
			case FieldType::I8:
				r_buf.write_i8(*reinterpret_cast<const int8_t *>(ptr));
				break;
			case FieldType::U8:
				r_buf.write_u8(*ptr);
				break;
			case FieldType::I16:
				r_buf.write_i16(*reinterpret_cast<const int16_t *>(ptr));
				break;
			case FieldType::U16:
				r_buf.write_u16(*reinterpret_cast<const uint16_t *>(ptr));
				break;
			case FieldType::I32:
				r_buf.write_i32(*reinterpret_cast<const int32_t *>(ptr));
				break;
			case FieldType::U32:
				r_buf.write_u32(*reinterpret_cast<const uint32_t *>(ptr));
				break;
			case FieldType::I64:
				r_buf.write_i64(*reinterpret_cast<const int64_t *>(ptr));
				break;
			case FieldType::U64:
				r_buf.write_u64(*reinterpret_cast<const uint64_t *>(ptr));
				break;
			case FieldType::F32:
				r_buf.write_f32(*reinterpret_cast<const float *>(ptr));
				break;
			case FieldType::F64:
				r_buf.write_f64(*reinterpret_cast<const double *>(ptr));
				break;
			default:
				// Vector/Color/Transform/StringFixed/Blob: raw fixed-size bytes
				// (IEEE-754 floats and LE ints on all Godot targets).
				r_buf.write_bytes(ptr, fd.storage_size());
				break;
		}
	}
}

bool deserialize_component(const ComponentSchema &p_schema, void *p_inst, BinaryBuffer &r_buf) {
	uint8_t *base = static_cast<uint8_t *>(p_inst);
	for (const FieldDescriptor &fd : p_schema.fields) {
		uint8_t *ptr = base + fd.offset;
		switch (fd.type) {
			case FieldType::Bool: {
				uint8_t v;
				if (!r_buf.read_u8(v)) {
					return false;
				}
				*ptr = v;
				break;
			}
			case FieldType::I8: {
				int8_t v;
				if (!r_buf.read_i8(v)) {
					return false;
				}
				*reinterpret_cast<int8_t *>(ptr) = v;
				break;
			}
			case FieldType::U8: {
				uint8_t v;
				if (!r_buf.read_u8(v)) {
					return false;
				}
				*ptr = v;
				break;
			}
			case FieldType::I16: {
				int16_t v;
				if (!r_buf.read_i16(v)) {
					return false;
				}
				*reinterpret_cast<int16_t *>(ptr) = v;
				break;
			}
			case FieldType::U16: {
				uint16_t v;
				if (!r_buf.read_u16(v)) {
					return false;
				}
				*reinterpret_cast<uint16_t *>(ptr) = v;
				break;
			}
			case FieldType::I32: {
				int32_t v;
				if (!r_buf.read_i32(v)) {
					return false;
				}
				*reinterpret_cast<int32_t *>(ptr) = v;
				break;
			}
			case FieldType::U32: {
				uint32_t v;
				if (!r_buf.read_u32(v)) {
					return false;
				}
				*reinterpret_cast<uint32_t *>(ptr) = v;
				break;
			}
			case FieldType::I64: {
				int64_t v;
				if (!r_buf.read_i64(v)) {
					return false;
				}
				*reinterpret_cast<int64_t *>(ptr) = v;
				break;
			}
			case FieldType::U64: {
				uint64_t v;
				if (!r_buf.read_u64(v)) {
					return false;
				}
				*reinterpret_cast<uint64_t *>(ptr) = v;
				break;
			}
			case FieldType::F32: {
				float v;
				if (!r_buf.read_f32(v)) {
					return false;
				}
				*reinterpret_cast<float *>(ptr) = v;
				break;
			}
			case FieldType::F64: {
				double v;
				if (!r_buf.read_f64(v)) {
					return false;
				}
				*reinterpret_cast<double *>(ptr) = v;
				break;
			}
			default:
				if (!r_buf.read_bytes(ptr, fd.storage_size())) {
					return false;
				}
				break;
		}
	}
	return true;
}

} // namespace vortaris
