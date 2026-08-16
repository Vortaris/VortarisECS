#pragma once

#include <cstdint>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/entity.h"

namespace vortaris {
class World;
}

class VECSComponent;

// Lightweight RefCounted wrapper around a vortaris::Entity handle. A wrapper
// is created fresh for every handle handed to GDScript; validity is checked
// against its owning world on every access (is_alive / is_valid).
class VECSEntity : public godot::RefCounted {
	GDCLASS(VECSEntity, godot::RefCounted)

public:
	static godot::Ref<VECSEntity> make(vortaris::World *p_world, vortaris::Entity p_entity);

	vortaris::Entity entity() const { return vortaris::Entity{ id_ }; }
	vortaris::World *world() const { return world_; }

	bool is_alive() const;
	int64_t get_id() const;
	bool has_component(const godot::String &p_type_name) const;
	godot::Array get_component_types() const;
	godot::Ref<VECSComponent> get_component(const godot::String &p_type_name) const;
	void add_component(const godot::String &p_type_name, const godot::Dictionary &p_fields);
	void remove_component(const godot::String &p_type_name);
	// One-call field sugar: e.getf("Position", "x") == e.get_component("Position").get_field("x").
	// Returns `p_default` when the component/field is missing (null Variant by default).
	godot::Variant getf(const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_default = godot::Variant()) const;
	void setf(const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_value);
	// Typed one-call field sugar (0.3.1): e.getf_int("Combatant", "hp") reads the
	// field as an int, dropping the explicit int()/float()/bool()/str() casts in
	// GDScript. Delegates to the matching VECSComponent typed getter; a missing
	// component/field returns the type's zero value (0 / 0.0 / false / "" / null).
	int64_t getf_int(const godot::String &p_comp, const godot::String &p_field) const;
	double getf_float(const godot::String &p_comp, const godot::String &p_field) const;
	bool getf_bool(const godot::String &p_comp, const godot::String &p_field) const;
	godot::String getf_string(const godot::String &p_comp, const godot::String &p_field) const;
	godot::Variant getf_vector(const godot::String &p_comp, const godot::String &p_field) const;
	bool equals(const godot::Ref<VECSEntity> &p_other) const;
	int64_t hash_value() const;

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr; // weak ref; entities are valid only while their world lives
	uint64_t id_ = 0;
};
