#pragma once

#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_type.h"
#include "../core/entity.h"

namespace vortaris {
class World;
struct Query;
}

class VECSEntity;

// Fluent, chainable query builder (mirrors GECS' QueryBuilder). Every term
// method returns `this` so calls can be chained in GDScript:
//
//   world.query().with_all(["Position", "Velocity"]).enabled().execute()
class VECSQueryBuilder : public godot::RefCounted {
	GDCLASS(VECSQueryBuilder, godot::RefCounted)

public:
	static godot::Ref<VECSQueryBuilder> make(vortaris::World *p_world);

	godot::Ref<VECSQueryBuilder> with_all(const godot::Array &p_names);
	godot::Ref<VECSQueryBuilder> with_any(const godot::Array &p_names);
	godot::Ref<VECSQueryBuilder> with_none(const godot::Array &p_names);
	godot::Ref<VECSQueryBuilder> enabled();
	godot::Ref<VECSQueryBuilder> changed(const godot::Array &p_names);
	// Predicate filter: only entities for which `predicate(entity)` is truthy are
	// returned. Applied by execute / execute_one / count.
	godot::Ref<VECSQueryBuilder> where(const godot::Callable &p_predicate);
	// Field-equality filter (0.3.1): only entities that have component `comp`
	// AND whose field `field` value equals `value` are returned. Unlike `where`
	// the comparison runs entirely in C++ (no per-entity GDScript callback), so
	// the CHANT "query the whole table then hand-compare the owner field" scans
	// collapse to one filtered query:
	//
	//   world.query().with_all(["Combatant"]).field_equals("Combatant", "owner", eid).execute()
	//
	// Chainable: each call adds one equality constraint; all must hold (AND).
	// Applied by execute / execute_one / count, alongside the other filters.
	// The comparison uses Variant equality, so an I64 field matches an int, an
	// F32 field matches a float, etc.
	godot::Ref<VECSQueryBuilder> field_equals(const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_value);
	// Sorts execute() results ascending by the field value of a component
	// (stable sort). execute_one / count are unaffected. The default order is
	// archetype creation order, then row order.
	godot::Ref<VECSQueryBuilder> order_by(const godot::String &p_comp, const godot::String &p_field);
	// Sorts execute() results ascending by entity id (stable sort).
	godot::Ref<VECSQueryBuilder> order_by_id();

	godot::Array execute();
	godot::Ref<VECSEntity> execute_one();
	int64_t count();
	// Microseconds spent in the most recent execute() call (debug/stats).
	int64_t get_last_execution_time_usec() const { return last_exec_usec_; }

protected:
	static void _bind_methods();

private:
	// One field_equals() constraint: an entity must carry `comp` and its field
	// `field` must read equal to `value`.
	struct FieldEquals {
		vortaris::ComponentTypeId comp = 0;
		godot::StringName field;
		godot::Variant value;
	};

	vortaris::World *world_ = nullptr;
	std::vector<vortaris::ComponentTypeId> all_;
	std::vector<vortaris::ComponentTypeId> any_;
	std::vector<vortaris::ComponentTypeId> none_;
	std::vector<vortaris::ComponentTypeId> changed_;
	std::vector<FieldEquals> field_equals_;
	bool enabled_only_ = false;
	godot::Callable where_;
	godot::String order_comp_;
	godot::String order_field_;
	bool order_by_id_ = false;
	int64_t last_exec_usec_ = 0;

	std::vector<vortaris::ComponentTypeId> resolve(const godot::Array &p_names) const;
	// True when the entity satisfies every field_equals() constraint. Called from
	// execute / execute_one / count; all comparisons happen in C++.
	bool _matches_field_equals(vortaris::Entity p_e) const;
	vortaris::Query _compile_query() const;
	// Baseline key = membership signature mixed with the sorted changed() ids,
	// so queries differing only in their changed filter get distinct baselines
	// and never advance each other's baseline (which would drop changes).
	uint64_t _baseline_key(const vortaris::Query &p_q) const;
};
