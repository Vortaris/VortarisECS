#pragma once

#include <cstdint>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

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
	godot::Ref<VECSComponent> get_component(const godot::String &p_type_name) const;
	void add_component(const godot::String &p_type_name, const godot::Dictionary &p_fields);
	void remove_component(const godot::String &p_type_name);
	bool equals(const godot::Ref<VECSEntity> &p_other) const;
	int64_t hash_value() const;

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr; // weak ref; entities are valid only while their world lives
	uint64_t id_ = 0;
};
