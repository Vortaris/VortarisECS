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
	godot::Variant get_field(const godot::String &p_name) const;
	void set_field(const godot::String &p_name, const godot::Variant &p_value);
	godot::Dictionary get_fields() const;

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr;
	vortaris::Entity entity_;
	vortaris::ComponentTypeId type_id_ = 0;
};
