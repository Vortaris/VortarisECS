#pragma once

#include <cstring>

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/component_schema.h"

namespace vortaris {

// Writes a raw field value (pointed to by p_src, described by p_fd) into a
// Godot Variant. Returns false if the field type is unsupported.
bool field_to_variant(const FieldDescriptor &p_fd, const void *p_src, godot::Variant &r_out);

// Copies a Variant value into the raw field buffer (p_dst). Returns false if
// the value cannot be represented by the field type.
bool field_from_variant(const FieldDescriptor &p_fd, void *p_dst, const godot::Variant &p_in);

// Element-level helpers for fixed-array fields. `p_type` is the element
// FieldType (FieldDescriptor::element_type for count > 1). Unlike
// field_to_variant/field_from_variant these do NOT handle StringFixed/Blob.
bool element_to_variant(FieldType p_type, const void *p_src, godot::Variant &r_out);
bool element_from_variant(FieldType p_type, void *p_dst, const godot::Variant &p_in);

// Serializes the raw bytes of one component instance into a binary buffer.
void component_bytes_to_variant_dict(const ComponentSchema &p_schema, const void *p_src, godot::Dictionary &r_out);

// Populates a zeroed component instance from a Dictionary of field values.
// Fields not present in the dictionary keep their (zeroed) value.
void component_dict_to_bytes(const ComponentSchema &p_schema, void *p_dst, const godot::Dictionary &p_in);

} // namespace vortaris
