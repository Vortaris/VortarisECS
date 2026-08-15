#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "column.h"
#include "component_type.h"
#include "entity.h"

namespace vortaris {

struct Query;

// An archetype groups all entities sharing the exact same component set into
// one flat array, with one SoA Column per component type. Query matching is a
// set-membership test; structural changes (add/remove component) move the
// entity between archetypes in O(1) via swap-remove.
struct Archetype {
	uint64_t signature = 0;
	std::vector<ComponentTypeId> component_ids; // sorted ascending
	std::vector<Entity> entities;
	std::vector<Column> columns; // parallel to component_ids
	std::vector<uint8_t> enabled; // 1 = enabled, 0 = disabled
	std::unordered_map<Entity, uint32_t> entity_to_row;

	bool has_component(ComponentTypeId p_t) const;
	size_t column_index(ComponentTypeId p_t) const; // SIZE_MAX if absent
	Column &column(ComponentTypeId p_t);
	const Column &column(ComponentTypeId p_t) const;

	uint32_t add_entity(Entity p_e, uint64_t p_change_tick);
	Entity remove_entity(Entity p_e); // returns the swap-moved entity, or null
	void set_enabled(uint32_t p_row, bool p_on);
	bool get_enabled(uint32_t p_row) const;

	bool matches(const Query &p_q) const;
};

} // namespace vortaris
