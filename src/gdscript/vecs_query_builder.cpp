#include "vecs_query_builder.h"

#include <algorithm>

#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/archetype.h"
#include "../core/component_registry.h"
#include "../core/world.h"
#include "vecs_entity.h"
#include "vecs_log.h"

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

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::where(const godot::Callable &p_predicate) {
	where_ = p_predicate;
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::order_by(const godot::String &p_comp, const godot::String &p_field) {
	order_comp_ = p_comp;
	order_field_ = p_field;
	return godot::Ref<VECSQueryBuilder>(this);
}

godot::Ref<VECSQueryBuilder> VECSQueryBuilder::order_by_id() {
	order_by_id_ = true;
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
	const uint64_t t0 = godot::Time::get_singleton()->get_ticks_usec();
	if (!world_) {
		return result;
	}

	vortaris::Query q = _compile_query();
	const auto &arches = world_->query_cache().match(q, world_->all_archetypes());

	bool has_changed_filter = !changed_.empty();
	uint64_t baseline = 0;
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

	std::vector<vortaris::Entity> collected;
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
			const vortaris::Entity e = a->entities[row];
			if (where_.is_valid()) {
				const godot::Variant v = where_.call(VECSEntity::make(world_, e));
				if (!v.booleanize()) {
					continue;
				}
			}
			collected.push_back(e);
		}
	}

	if (order_by_id_) {
		std::stable_sort(collected.begin(), collected.end(), [](const vortaris::Entity &a, const vortaris::Entity &b) {
			return a.id < b.id;
		});
	} else if (!order_comp_.is_empty()) {
		std::stable_sort(collected.begin(), collected.end(), [this](const vortaris::Entity &a, const vortaris::Entity &b) {
			godot::Ref<VECSEntity> wa = VECSEntity::make(world_, a);
			godot::Ref<VECSEntity> wb = VECSEntity::make(world_, b);
			return wa->getf(order_comp_, order_field_) < wb->getf(order_comp_, order_field_);
		});
	}

	for (const vortaris::Entity &e : collected) {
		result.append(VECSEntity::make(world_, e));
	}

	if (has_changed_filter) {
		world_->set_changed_baseline(key, world_->change_tick());
	}
	last_exec_usec_ = static_cast<int64_t>(godot::Time::get_singleton()->get_ticks_usec() - t0);
	if (vortaris::verbose_active()) {
		vortaris::log_verbose("query executed (" + godot::String::num_int64(result.size()) + " results, " +
				godot::String::num_int64(last_exec_usec_) + " usec, changed_filter=" + (has_changed_filter ? godot::String("yes") : godot::String("no")) + ")");
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
	uint64_t baseline = 0;
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
			const vortaris::Entity e = a->entities[row];
			if (where_.is_valid()) {
				const godot::Variant v = where_.call(VECSEntity::make(world_, e));
				if (!v.booleanize()) {
					continue;
				}
			}
			result = VECSEntity::make(world_, e);
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
	const bool has_where = where_.is_valid();
	for (vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			if (enabled_only_ && !a->get_enabled(row)) {
				continue;
			}
			if (has_where) {
				const vortaris::Entity e = a->entities[row];
				const godot::Variant v = where_.call(VECSEntity::make(world_, e));
				if (!v.booleanize()) {
					continue;
				}
			}
			++n;
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
	ClassDB::bind_method(D_METHOD("where", "predicate"), &VECSQueryBuilder::where);
	ClassDB::bind_method(D_METHOD("order_by", "comp", "field"), &VECSQueryBuilder::order_by);
	ClassDB::bind_method(D_METHOD("order_by_id"), &VECSQueryBuilder::order_by_id);
	ClassDB::bind_method(D_METHOD("execute"), &VECSQueryBuilder::execute);
	ClassDB::bind_method(D_METHOD("execute_one"), &VECSQueryBuilder::execute_one);
	ClassDB::bind_method(D_METHOD("count"), &VECSQueryBuilder::count);
	ClassDB::bind_method(D_METHOD("get_last_execution_time_usec"), &VECSQueryBuilder::get_last_execution_time_usec);
}
