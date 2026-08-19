#include "vecs_observer.h"

#include <algorithm>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/gdextension_interface_loader.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"
#include "../core/query.h"
#include "../core/world.h"
#include "vecs_entity.h"
#include "vecs_settings.h"
#include "vecs_world.h"

VARIANT_ENUM_CAST(VECSObserver::Event);
VARIANT_ENUM_CAST(VECSObserver::FlushMode);

VECSObserver::VECSObserver() {
	// Seed the CHANGED throttle from the project setting so newly created
	// observers pick up `vortarisecs/observer/default_throttle_tick` (0 by
	// default => no throttling). A script can still call set_throttle_tick()
	// explicitly to override it.
	throttle_tick_ = vortaris::get_default_throttle_tick();
}

void VECSObserver::set_callback(const godot::Callable &p_callable) {
	callback_ = p_callable;
}

void VECSObserver::set_events(int p_mask) {
	event_mask_ = p_mask;
}

void VECSObserver::on_added() {
	event_mask_ |= vortaris::EVENT_ADDED;
}

void VECSObserver::on_removed() {
	event_mask_ |= vortaris::EVENT_REMOVED;
}

void VECSObserver::on_changed() {
	event_mask_ |= vortaris::EVENT_CHANGED;
}

void VECSObserver::on_matched() {
	event_mask_ |= vortaris::EVENT_MATCHED;
}

void VECSObserver::on_unmatched() {
	event_mask_ |= vortaris::EVENT_UNMATCHED;
}

void VECSObserver::on_custom() {
	event_mask_ |= vortaris::EVENT_CUSTOM;
}

void VECSObserver::set_components(const godot::Array &p_names) {
	component_filter_.clear();
	for (int i = 0; i < p_names.size(); ++i) {
		godot::StringName n(p_names[i]);
		vortaris::ComponentTypeId t = vortaris::ComponentRegistry::instance().id_of(n);
		if (t != vortaris::INVALID_COMPONENT_TYPE) {
			component_filter_.push_back(t);
		}
	}
}

godot::Array VECSObserver::get_components() const {
	godot::Array out;
	for (vortaris::ComponentTypeId t : component_filter_) {
		out.append(godot::String(vortaris::ComponentRegistry::instance().name_of(t)));
	}
	return out;
}

void VECSObserver::set_fields(const godot::Array &p_names) {
	field_filter_.clear();
	for (int i = 0; i < p_names.size(); ++i) {
		field_filter_.push_back(godot::StringName(godot::String(p_names[i])));
	}
}

godot::Array VECSObserver::get_fields() const {
	godot::Array out;
	for (const godot::StringName &f : field_filter_) {
		out.append(godot::String(f));
	}
	return out;
}

void VECSObserver::set_throttle_tick(int64_t p_ticks) {
	throttle_tick_ = p_ticks > 0 ? p_ticks : 0;
}

void VECSObserver::set_match_components(const godot::Array &p_names) {
	match_query_.clear();
	for (int i = 0; i < p_names.size(); ++i) {
		godot::StringName n(p_names[i]);
		vortaris::ComponentTypeId t = vortaris::ComponentRegistry::instance().id_of(n);
		if (t != vortaris::INVALID_COMPONENT_TYPE) {
			match_query_.push_back(t);
		}
	}
}

godot::Array VECSObserver::get_match_components() const {
	godot::Array out;
	for (vortaris::ComponentTypeId t : match_query_) {
		out.append(godot::String(vortaris::ComponentRegistry::instance().name_of(t)));
	}
	return out;
}

void VECSObserver::set_custom_event_name(const godot::String &p_name) {
	custom_name_ = p_name;
}

void VECSObserver::set_flush_mode(int p_v) {
	flush_mode_ = p_v;
}

void VECSObserver::set_world(VECSWorld *p_world) {
	world_ = p_world;
	// Capture the world's identity NOW (audit fix): NOTIFICATION_PREDELETE may
	// run after the world is already freed, and calling get_instance_id() on a
	// freed object is itself a use-after-free. The stored id lets the predelete
	// path do the liveness check without ever touching the raw pointer blindly.
	world_instance_id_ = (p_world != nullptr) ? p_world->get_instance_id() : 0;
}

bool VECSObserver::has_match_events() const {
	return (event_mask_ & (vortaris::EVENT_MATCHED | vortaris::EVENT_UNMATCHED)) != 0;
}

void VECSObserver::handle_event(vortaris::ObserverEventType p_type, vortaris::Entity p_entity, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
	int ev = -1;
	switch (p_type) {
		case vortaris::ObserverEventType::Added:
			ev = ADDED;
			break;
		case vortaris::ObserverEventType::Removed:
			ev = REMOVED;
			break;
		case vortaris::ObserverEventType::Changed:
			ev = CHANGED;
			break;
		case vortaris::ObserverEventType::Matched:
			ev = MATCHED;
			break;
		case vortaris::ObserverEventType::Unmatched:
			ev = UNMATCHED;
			break;
		case vortaris::ObserverEventType::Custom:
			ev = CUSTOM;
			break;
	}
	if (ev < 0) {
		return;
	}

	// Field-level Changed filter: when a field filter is configured, only
	// CHANGED events whose field name is in the set are delivered. Component /
	// custom events carry no field name and are not affected.
	if (p_type == vortaris::ObserverEventType::Changed && !field_filter_.empty()) {
		if (p_name.is_empty()) {
			return; // value changed at the component level (no field info): skip
		}
		godot::StringName field_name(p_name);
		if (std::find(field_filter_.begin(), field_filter_.end(), field_name) == field_filter_.end()) {
			return; // field not subscribed
		}
	}

	// Changed throttle: use the world's monotonic change tick as a deterministic
	// clock. Suppress delivery when fewer than throttle_tick_ ticks have elapsed
	// since the last delivered CHANGED event.
	if (p_type == vortaris::ObserverEventType::Changed && throttle_tick_ > 0 && world_) {
		const int64_t now = static_cast<int64_t>(world_->core().change_tick());
		if (last_emit_tick_ >= 0 && now - last_emit_tick_ < throttle_tick_) {
			return;
		}
		last_emit_tick_ = now;
	}

	godot::Ref<VECSEntity> entity = VECSEntity::make(world_ ? &world_->core() : nullptr, p_entity);
	// A plain set_callback() callable takes precedence over a _script_each
	// override; the C++ _each override point remains for native subclasses.
	if (callback_.is_valid()) {
		callback_.call(static_cast<int64_t>(ev), entity.ptr(), p_payload);
	} else if (_gdvirtual__script_each_overridden()) {
		_gdvirtual__script_each_call(static_cast<int64_t>(ev), entity.ptr(), p_payload);
	} else {
		_each(ev, p_entity, p_payload);
	}

	if (flush_mode_ == PER_CALLBACK && world_) {
		world_->core().flush_command_buffers();
	}
}

void VECSObserver::seed_membership() {
	if (!world_ || match_query_.empty()) {
		return;
	}
	vortaris::World &w = world_->core();
	vortaris::Query q;
	q.all = match_query_;
	std::sort(q.all.begin(), q.all.end());
	const auto &arches = w.query_cache().match(q, w.all_archetypes());
	for (const vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			vortaris::Entity e = a->entities[row];
			if (membership_.insert(e).second) {
				handle_event(vortaris::ObserverEventType::Matched, e, 0, godot::String(), godot::Variant());
			}
		}
	}
}

void VECSObserver::evaluate_match(vortaris::Entity p_entity) {
	if (!world_ || match_query_.empty()) {
		return;
	}
	vortaris::World &w = world_->core();
	bool matches = true;
	for (vortaris::ComponentTypeId t : match_query_) {
		if (!w.has(p_entity, t)) {
			matches = false;
			break;
		}
	}
	bool in_membership = membership_.count(p_entity) > 0;
	if (matches && !in_membership) {
		membership_.insert(p_entity);
		handle_event(vortaris::ObserverEventType::Matched, p_entity, 0, godot::String(), godot::Variant());
	} else if (!matches && in_membership) {
		membership_.erase(p_entity);
		handle_event(vortaris::ObserverEventType::Unmatched, p_entity, 0, godot::String(), godot::Variant());
	}
}

void VECSObserver::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE && world_) {
		// The VECSWorld singleton may be freed before this node (extension unload
		// order is not guaranteed). Resolve the stored instance id (captured at
		// set_world time — never dereference world_ before the liveness check,
		// that would be a use-after-free) so a stale pointer cannot be touched.
		GDExtensionObjectPtr live = world_instance_id_ != 0
				? godot::gdextension_interface::object_get_instance_from_id(world_instance_id_)
				: nullptr;
		if (live != nullptr) {
			world_->remove_observer(this);
		}
		world_ = nullptr;
		world_instance_id_ = 0;
	}
}

void VECSObserver::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("set_callback", "callable"), &VECSObserver::set_callback);
	ClassDB::bind_method(D_METHOD("get_callback"), &VECSObserver::get_callback);
	ClassDB::bind_method(D_METHOD("set_events", "mask"), &VECSObserver::set_events);
	ClassDB::bind_method(D_METHOD("get_events"), &VECSObserver::get_events);
	ClassDB::bind_method(D_METHOD("on_added"), &VECSObserver::on_added);
	ClassDB::bind_method(D_METHOD("on_removed"), &VECSObserver::on_removed);
	ClassDB::bind_method(D_METHOD("on_changed"), &VECSObserver::on_changed);
	ClassDB::bind_method(D_METHOD("on_matched"), &VECSObserver::on_matched);
	ClassDB::bind_method(D_METHOD("on_unmatched"), &VECSObserver::on_unmatched);
	ClassDB::bind_method(D_METHOD("on_custom"), &VECSObserver::on_custom);
	ClassDB::bind_method(D_METHOD("set_components", "names"), &VECSObserver::set_components);
	ClassDB::bind_method(D_METHOD("get_components"), &VECSObserver::get_components);
	ClassDB::bind_method(D_METHOD("set_fields", "names"), &VECSObserver::set_fields);
	ClassDB::bind_method(D_METHOD("get_fields"), &VECSObserver::get_fields);
	ClassDB::bind_method(D_METHOD("set_throttle_tick", "ticks"), &VECSObserver::set_throttle_tick);
	ClassDB::bind_method(D_METHOD("get_throttle_tick"), &VECSObserver::get_throttle_tick);
	ClassDB::bind_method(D_METHOD("set_match_components", "names"), &VECSObserver::set_match_components);
	ClassDB::bind_method(D_METHOD("get_match_components"), &VECSObserver::get_match_components);
	ClassDB::bind_method(D_METHOD("set_custom_event_name", "name"), &VECSObserver::set_custom_event_name);
	ClassDB::bind_method(D_METHOD("get_custom_event_name"), &VECSObserver::get_custom_event_name);
	ClassDB::bind_method(D_METHOD("set_flush_mode", "value"), &VECSObserver::set_flush_mode);
	ClassDB::bind_method(D_METHOD("get_flush_mode"), &VECSObserver::get_flush_mode);
	ClassDB::bind_method(D_METHOD("get_world"), &VECSObserver::get_world);
	ClassDB::add_property("VECSObserver", PropertyInfo(Variant::INT, "events"), "set_events", "get_events");
	ClassDB::add_property("VECSObserver", PropertyInfo(Variant::INT, "flush_mode"), "set_flush_mode", "get_flush_mode");
	ClassDB::add_property("VECSObserver", PropertyInfo(Variant::STRING, "custom_event_name"), "set_custom_event_name", "get_custom_event_name");
	GDVIRTUAL_BIND(_script_each, "event", "entity", "payload");
	BIND_ENUM_CONSTANT(ADDED);
	BIND_ENUM_CONSTANT(REMOVED);
	BIND_ENUM_CONSTANT(CHANGED);
	BIND_ENUM_CONSTANT(MATCHED);
	BIND_ENUM_CONSTANT(UNMATCHED);
	BIND_ENUM_CONSTANT(CUSTOM);
	BIND_ENUM_CONSTANT(PER_CALLBACK);
	BIND_ENUM_CONSTANT(MANUAL);
}
