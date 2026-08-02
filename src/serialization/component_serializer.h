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

} // namespace vortaris
