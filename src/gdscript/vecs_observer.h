#pragma once

#include <unordered_set>
#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/component_type.h"
#include "../core/entity.h"
#include "../core/observer_dispatch.h"

#include "vecs_entity.h"

namespace vortaris {
class World;
}

class VECSWorld;

// Reactive, event-driven observer. C++ observers override _each(); GDScript
// observers override _script_each(event, entity, payload). An observer
// subscribes to a set of component types and event kinds; MATCHED/UNMATCHED
// observers additionally track membership of a component-set query.
class VECSObserver : public godot::Node {
	GDCLASS(VECSObserver, godot::Node)

public:
	enum Event {
		ADDED = 0,
		REMOVED = 1,
		CHANGED = 2,
		MATCHED = 3,
		UNMATCHED = 4,
		CUSTOM = 5,
	};

	enum FlushMode {
		PER_CALLBACK = 0,
		MANUAL = 1,
	};

	// --- C++ override point ---
	virtual void _each(int p_event, vortaris::Entity p_entity, const godot::Variant &p_payload) {}

	// --- GDScript override point ---
	GDVIRTUAL3(_script_each, int64_t, VECSEntity *, godot::Variant);

	VECSObserver();

	// --- config ---
	// Sets a plain GDScript Callable to run for every delivered event, instead of
	// overriding _script_each in a subclass. The callable signature is
	//   func(event: int, entity: VECSEntity, payload: Variant) -> void
	// This (plus the class being directly instantiable since 0.3.1) lets CHANT
	// write `VECSObserver.new()` without subclassing, e.g.
	//   var obs: VECSObserver = VECSObserver.new()
	//   obs.set_callback(func(event, entity, payload): ...)
	//   obs.on_changed(); obs.set_components(["Combatant"])
	//   world.add_observer(obs)
	// When both a callback and a _script_each override exist, the callback wins.
	void set_callback(const godot::Callable &p_callable);
	godot::Callable get_callback() const { return callback_; }
	void set_events(int p_mask);
	int get_events() const { return event_mask_; }
	void on_added();
	void on_removed();
	void on_changed();
	void on_matched();
	void on_unmatched();
	void on_custom();
	void set_components(const godot::Array &p_names);
	godot::Array get_components() const;
	// Field-level Changed subscription. When non-empty, only CHANGED events whose
	// field name is listed here are delivered; ADDED/REMOVED/MATCHED/UNMATCHED/
	// CUSTOM are unaffected.
	void set_fields(const godot::Array &p_names);
	godot::Array get_fields() const;
	// Throttles CHANGED delivery: at most one callback per `ticks` change-clock
	// ticks (the world's monotonic change_tick). 0 disables throttling.
	void set_throttle_tick(int64_t p_ticks);
	int64_t get_throttle_tick() const { return throttle_tick_; }
	void set_match_components(const godot::Array &p_names);
	godot::Array get_match_components() const;
	void set_custom_event_name(const godot::String &p_name);
	godot::String get_custom_event_name() const { return custom_name_; }
	void set_flush_mode(int p_v);
	int get_flush_mode() const { return flush_mode_; }

	// --- wiring (used by VECSWorld) ---
	void set_world(VECSWorld *p_world);
	VECSWorld *get_world() const { return world_; }
	void set_observer_id(vortaris::ObserverId p_id) { observer_id_ = p_id; }
	vortaris::ObserverId observer_id() const { return observer_id_; }
	bool has_match_events() const;
	const std::vector<vortaris::ComponentTypeId> &component_filter() const { return component_filter_; }

	// --- dispatch entry ---
	void handle_event(vortaris::ObserverEventType p_type, vortaris::Entity p_entity, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload);
	void seed_membership();
	void evaluate_match(vortaris::Entity p_entity);

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	VECSWorld *world_ = nullptr;
	// Captured at set_world() time: lets the PREDELETE liveness check avoid
	// dereferencing world_ when the world was already freed (audit fix).
	uint64_t world_instance_id_ = 0;
	godot::Callable callback_;
	int event_mask_ = 0;
	std::vector<vortaris::ComponentTypeId> component_filter_;
	std::vector<godot::StringName> field_filter_;
	int64_t throttle_tick_ = 0;
	int64_t last_emit_tick_ = -1;
	std::vector<vortaris::ComponentTypeId> match_query_;
	godot::String custom_name_;
	int flush_mode_ = PER_CALLBACK;
	vortaris::ObserverId observer_id_ = 0;
	std::unordered_set<vortaris::Entity> membership_;
};
