#pragma once

#include "binary_buffer.h"
#include "component_serializer.h"

namespace vortaris {

class World;

// Serializes the full world state (all archetypes, entities, components) in a
// deterministic order: archetypes sorted by signature, entities in row order,
// components in ascending type-id order. Deserializing into a fresh world
// reproduces the exact entity/component layout.
//
// Format: u16 version | u32 entity_count | per entity: u64 id | u16 ncomp |
//         (u32 type_id | component bytes)*
constexpr uint16_t SNAPSHOT_VERSION = 1;

void serialize_world_snapshot(const World &p_world, BinaryBuffer &r_out);
bool deserialize_world_snapshot(World &p_world, BinaryBuffer &r_in);

} // namespace vortaris
