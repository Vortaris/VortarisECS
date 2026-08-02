#include "archetype.h"

#include <algorithm>

#include "query.h"

namespace vortaris {

bool Archetype::has_component(ComponentTypeId p_t) const {
	return std::binary_search(component_ids.begin(), component_ids.end(), p_t);
}

size_t Archetype::column_index(ComponentTypeId p_t) const {
	auto it = std::lower_bound(component_ids.begin(), component_ids.end(), p_t);
	if (it == component_ids.end() || *it != p_t) {
		return SIZE_MAX;
	}
	return static_cast<size_t>(std::distance(component_ids.begin(), it));
}

Column &Archetype::column(ComponentTypeId p_t) {
	return columns[column_index(p_t)];
}

const Column &Archetype::column(ComponentTypeId p_t) const {
	return columns[column_index(p_t)];
}

uint32_t Archetype::add_entity(Entity p_e, uint32_t p_change_tick) {
	uint32_t row = static_cast<uint32_t>(entities.size());
	entities.push_back(p_e);
	enabled.push_back(1);
	for (Column &col : columns) {
		col.grow();
		if (col.has_versions()) {
			col.set_version(row, p_change_tick);
		}
	}
	entity_to_row[p_e] = row;
	return row;
}

Entity Archetype::remove_entity(Entity p_e) {
	auto it = entity_to_row.find(p_e);
	if (it == entity_to_row.end()) {
		return Entity{};
	}
	uint32_t row = it->second;
	entity_to_row.erase(it);

	uint32_t last = static_cast<uint32_t>(entities.size()) - 1;
	Entity moved{};
	if (row != last) {
		moved = entities[last];
		entities[row] = moved;
		entity_to_row[moved] = row;
		for (Column &col : columns) {
			col.swap_remove(row);
		}
		enabled[row] = enabled[last];
	} else {
		for (Column &col : columns) {
			col.pop_back();
		}
	}
	entities.pop_back();
	enabled.pop_back();
	return moved;
}

void Archetype::set_enabled(uint32_t p_row, bool p_on) {
	if (p_row < enabled.size()) {
		enabled[p_row] = p_on ? 1 : 0;
	}
}

bool Archetype::get_enabled(uint32_t p_row) const {
	return p_row < enabled.size() && enabled[p_row] != 0;
}

bool Archetype::matches(const Query &p_q) const {
	for (ComponentTypeId id : p_q.all) {
		if (!has_component(id)) {
			return false;
		}
	}
	if (!p_q.any.empty()) {
		bool ok = false;
		for (ComponentTypeId id : p_q.any) {
			if (has_component(id)) {
				ok = true;
				break;
			}
		}
		if (!ok) {
			return false;
		}
	}
	for (ComponentTypeId id : p_q.none) {
		if (has_component(id)) {
			return false;
		}
	}
	return true;
}

} // namespace vortaris
