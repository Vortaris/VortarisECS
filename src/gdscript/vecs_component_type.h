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
	// Fixed-array length of a field: 1 for scalars, >1 for arrays, 0 for unknown.
	int64_t get_field_count(const godot::String &p_field) const;
	// Type name of a field as a String ("F32", "Vector3", ...). For array fields
	// (count > 1) returns "Array:F32" etc. Empty string for unknown fields.
	godot::String get_field_type(const godot::String &p_field) const;
	int64_t get_field_sync_priority(const godot::String &p_field) const;
	bool get_field_is_networked(const godot::String &p_field) const;

protected:
	static void _bind_methods();

private:
	vortaris::ComponentTypeId type_id_ = 0;
};
