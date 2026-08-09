#include "vecs_query_builder.h"

#include <algorithm>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/archetype.h"
#include "../core/component_registry.h"
#include "../core/world.h"
#include "vecs_entity.h"

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::make(vortaris::World *p_world) {
	godot::Ref<VECSQueryBuilder> ref;
	ref.instantiate();
	ref->world_ = p_world;
	return ref;
}

std::vector<vortaris::ComponentTypeId> VECSQueryBuilder::resolve(const godot::Array &p_names) const {
	std::vector<vortaris::ComponentTypeId> ids;
	for (int i = 0; i < p_names.size(); ++i) {
		godot::String s = p_names[i];
		vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(s));
		if (t != vortaris::INVALID_COMPONENT_TYPE) {
			ids.push_back(t);
		} else {
			ERR_PRINT("VortarisECS: unknown component '" + s + "' in query.");
		}
	}
	return ids;
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::with_all(const godot::Array &p_names) {
	std::vector<vortaris::ComponentTypeId> ids = resolve(p_names);
	all_.insert(all_.end(), ids.begin(), ids.end());
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::with_any(const godot::Array &p_names) {
	std::vector<vortaris::ComponentTypeId> ids = resolve(p_names);
	any_.insert(any_.end(), ids.begin(), ids.end());
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::with_none(const godot::Array &p_names) {
	std::vector<vortaris::ComponentTypeId> ids = resolve(p_names);
	none_.insert(none_.end(), ids.begin(), ids.end());
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::enabled() {
	enabled_only_ = true;
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::changed(const godot::Array &p_names) {
	std::vector<vortaris::ComponentTypeId> ids = resolve(p_names);
	changed_.insert(changed_.end(), ids.begin(), ids.end());
	return godot::Ref<VECSQueryBuilder>(this);
}

vortaris::Query VECSQueryBuilder::_compile_query() const {
	vortaris::Query q;
	q.all = all_;
	q.any = any_;
	q.none = none_;
	std::sort(q.all.begin(), q.all.end());
	std::sort(q.any.begin(), q.any.end());
	std::sort(q.none.begin(), q.none.end());
	return q;
}

uint64_t VECSQueryBuilder::_baseline_key(const vortaris::Query &p_q) const {
	uint64_t h = p_q.membership_signature();
	std::vector<vortaris::ComponentTypeId> ids = changed_;
	std::sort(ids.begin(), ids.end());
	// Domain tag + FNV-1a mix of the changed set, matching the mix used in
	// Query::membership_signature.
	h ^= 0x9E3779B97F4A7C15ull;
	h *= 1099511628211ull;
	for (vortaris::ComponentTypeId id : ids) {
		uint32_t v = id;
		for (int i = 0; i < 4; ++i) {
			h ^= (v & 0xFF);
			h *= 1099511628211ull;
			v >>= 8;
		}
	}
	return h;
}

godot::Array VECSQueryBuilder::execute() {
	godot::Array result;
	if (!world_) {
		return result;
	}

	vortaris::Query q = _compile_query();
	const auto &arches = world_->query_cache().match(q, world_->all_archetypes());

	bool has_changed_filter = !changed_.empty();
	uint32_t baseline = 0;
	uint64_t key = 0;
	if (has_changed_filter) {
		key = _baseline_key(q);
		bool existed = false;
		baseline = world_->changed_baseline(key, &existed);
		for (vortaris::Archetype *a : arches) {
			for (vortaris::ComponentTypeId t : changed_) {
				if (a->has_component(t)) {
					a->column(t).ensure_versions(world_->change_tick());
				}
			}
		}
	}

	for (vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			if (enabled_only_ && !a->get_enabled(row)) {
				continue;
			}
			if (has_changed_filter) {
				bool any_changed = false;
				for (vortaris::ComponentTypeId t : changed_) {
					if (a->has_component(t) && a->column(t).row_changed_since(row, baseline)) {
						any_changed = true;
						break;
					}
				}
				if (!any_changed) {
					continue;
				}
			}
			result.append(VECSEntity::make(world_, a->entities[row]));
		}
	}

	if (has_changed_filter) {
		world_->set_changed_baseline(key, world_->change_tick());
	}
	return result;
}

godot::Ref<VECSEntity> VECSQueryBuilder::execute_one() {
	godot::Ref<VECSEntity> result;
	if (!world_) {
		return result;
	}
	vortaris::Query q = _compile_query();
	const auto &arches = world_->query_cache().match(q, world_->all_archetypes());

	bool has_changed_filter = !changed_.empty();
	uint32_t baseline = 0;
	uint64_t key = 0;
	if (has_changed_filter) {
		key = _baseline_key(q);
		bool existed = false;
		baseline = world_->changed_baseline(key, &existed);
		for (vortaris::Archetype *a : arches) {
			for (vortaris::ComponentTypeId t : changed_) {
				if (a->has_component(t)) {
					a->column(t).ensure_versions(world_->change_tick());
				}
			}
		}
	}

	for (vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			if (enabled_only_ && !a->get_enabled(row)) {
				continue;
			}
			if (has_changed_filter) {
				bool any_changed = false;
				for (vortaris::ComponentTypeId t : changed_) {
					if (a->has_component(t) && a->column(t).row_changed_since(row, baseline)) {
						any_changed = true;
						break;
					}
				}
				if (!any_changed) {
					continue;
				}
			}
			result = VECSEntity::make(world_, a->entities[row]);
			break;
		}
		if (result.is_valid()) {
			break;
		}
	}

	// Advance the baseline even when nothing matched, mirroring execute().
	if (has_changed_filter) {
		world_->set_changed_baseline(key, world_->change_tick());
	}
	return result;
}

int64_t VECSQueryBuilder::count() {
	if (!world_) {
		return 0;
	}
	vortaris::Query q = _compile_query();
	const auto &arches = world_->query_cache().match(q, world_->all_archetypes());
	int64_t n = 0;
	for (vortaris::Archetype *a : arches) {
		if (enabled_only_) {
			for (size_t row = 0; row < a->entities.size(); ++row) {
				if (a->get_enabled(row)) {
					++n;
				}
			}
		} else {
			n += static_cast<int64_t>(a->entities.size());
		}
	}
	return n;
}

void VECSQueryBuilder::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("with_all", "names"), &VECSQueryBuilder::with_all);
	ClassDB::bind_method(D_METHOD("with_any", "names"), &VECSQueryBuilder::with_any);
	ClassDB::bind_method(D_METHOD("with_none", "names"), &VECSQueryBuilder::with_none);
	ClassDB::bind_method(D_METHOD("enabled"), &VECSQueryBuilder::enabled);
	ClassDB::bind_method(D_METHOD("changed", "names"), &VECSQueryBuilder::changed, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("execute"), &VECSQueryBuilder::execute);
	ClassDB::bind_method(D_METHOD("execute_one"), &VECSQueryBuilder::execute_one);
	ClassDB::bind_method(D_METHOD("count"), &VECSQueryBuilder::count);
}
