#pragma once

#include <vector>

#include "../core/component_schema.h"
#include "binary_buffer.h"

namespace vortaris {

// Serializes one component instance to a deterministic binary form, walking
// the schema fields in order (skipping struct padding). Reads back through
// deserialize_component. All practical Godot targets are little-endian, so
// raw IEEE-754 floats and ints round-trip bit-identically.
void serialize_component(const ComponentSchema &p_schema, const void *p_inst, BinaryBuffer &r_buf);
bool deserialize_component(const ComponentSchema &p_schema, void *p_inst, BinaryBuffer &r_buf);

// Total byte length serialize_component() writes for one component instance.
// Must stay byte-for-byte consistent with serialize_component (scalar fields
// write their fixed width; everything else writes storage_size bytes, with
// StringFixed writing content + zero padding to its full capacity). Used by the
// network layer to dry-run a packet before any write happens.
size_t serialized_component_size(const ComponentSchema &p_schema);

// ---- field-mask variants (0.4.0 wire v2) ----
// Serialize/deserialize only the fields whose mask bit is set, in schema
// order. p_mask must have one entry per schema field (an entry is treated as
// "included" when it is missing from a too-short mask, so an all-empty mask
// degenerates to the full component). Wire v2 uses these to replicate
// per-field sync_priority buckets and to keep SYNC_LOCAL fields off the wire.
void serialize_component_fields(const ComponentSchema &p_schema, const void *p_inst,
		BinaryBuffer &r_buf, const std::vector<bool> &p_mask);
bool deserialize_component_fields(const ComponentSchema &p_schema, void *p_inst,
		BinaryBuffer &r_buf, const std::vector<bool> &p_mask);
size_t serialized_component_fields_size(const ComponentSchema &p_schema, const std::vector<bool> &p_mask);
// Byte length of a bitmask over p_field_count fields.
size_t field_mask_bytes(size_t p_field_count);

} // namespace vortaris
