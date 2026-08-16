#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/component_type.h"
#include "../core/entity.h"

namespace vortaris {
class World;
}

// A transient, field-level accessor for one component instance. Obtained from
// VECSEntity::get_component(); reads/writes go straight to the archetype
// column so every set_field() is reflected immediately (and marked changed).
class VECSComponent : public godot::RefCounted {
	GDCLASS(VECSComponent, godot::RefCounted)

public:
	static godot::Ref<VECSComponent> make(vortaris::World *p_world, vortaris::Entity p_entity, vortaris::ComponentTypeId p_type);

	bool is_valid() const;
	godot::String get_type_name() const;
	godot::Variant get_field(const godot::String &p_name, const godot::Variant &p_default = godot::Variant()) const;
	void set_field(const godot::String &p_name, const godot::Variant &p_value);
	godot::Dictionary get_fields() const;
	// Fixed-array field convenience. The field is stored as a Godot Array when
	// count > 1; these helpers read/write a single element.
	int64_t get_field_count(const godot::String &p_name) const;
	godot::Variant get_array_element(const godot::String &p_name, int64_t p_index) const;
	bool set_array_element(const godot::String &p_name, int64_t p_index, const godot::Variant &p_value);
	// Returns true when the field [name] equals `p_value` (scalar fields) or when
	// any element of a fixed-array field equals it. Kills the CHANT
	// "rebuild the array and scan for a 0-sentinel" idiom: check membership in
	// one call. Comparison uses Variant equality, so an F32 array element matches
	// an int 0 and vice versa.
	bool field_contains(const godot::String &p_name, const godot::Variant &p_value) const;
	// Typed field accessors (0.3.1). Each reads the field (as get_field does)
	// and returns it as the requested Variant type, so GDScript callers can drop
	// the explicit int()/float()/bool()/str() casts (CHANT had 162 of those).
	// A missing component/field returns that type's zero value: 0 / 0.0 / false /
	// "" / null. Numeric getters truncate float->int and coerce bool as 0/1;
	// get_string() stringifies numbers/bools/vectors like GDScript str();
	// get_vector() returns the value unchanged when it is a Vector2/2i/3/3i/4/4i,
	// otherwise a null Variant.
	int64_t get_int(const godot::String &p_name) const;
	double get_float(const godot::String &p_name) const;
	bool get_bool(const godot::String &p_name) const;
	godot::String get_string(const godot::String &p_name) const;
	godot::Variant get_vector(const godot::String &p_name) const;

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr;
	vortaris::Entity entity_;
	vortaris::ComponentTypeId type_id_ = 0;
};
