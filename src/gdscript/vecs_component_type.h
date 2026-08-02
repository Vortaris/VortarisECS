#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "../core/component_type.h"

// Registry metadata for one registered component type (name, field list,
// network settings). Obtained from VECSWorld::get_component_type().
class VECSComponentType : public godot::RefCounted {
	GDCLASS(VECSComponentType, godot::RefCounted)

public:
	static godot::Ref<VECSComponentType> make(vortaris::ComponentTypeId p_type);

	godot::String get_name() const;
	int64_t get_id() const;
	int64_t get_size() const;
	godot::Array get_field_names() const;
	int64_t get_field_sync_priority(const godot::String &p_field) const;
	bool get_field_is_networked(const godot::String &p_field) const;

protected:
	static void _bind_methods();

private:
	vortaris::ComponentTypeId type_id_ = 0;
};
