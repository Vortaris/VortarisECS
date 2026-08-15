#pragma once

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

} // namespace vortaris
