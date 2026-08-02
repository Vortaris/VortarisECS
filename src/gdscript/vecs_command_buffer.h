#pragma once

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "../core/entity.h"

namespace vortaris {
class World;
}

class VECSEntity;

// GDScript-facing handle to a world's CommandBuffer. Ops are queued until
// flush() (or the world's next process()) executes them inside the deferred
// structural-change window.
class VECSCommandBuffer : public godot::RefCounted {
	GDCLASS(VECSCommandBuffer, godot::RefCounted)

public:
	static godot::Ref<VECSCommandBuffer> make(vortaris::World *p_world);

	void add_component(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_type_name, const godot::Dictionary &p_fields);
	void remove_component(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_type_name);
	void add_entity(const godot::Ref<VECSEntity> &p_entity);
	void remove_entity(const godot::Ref<VECSEntity> &p_entity);
	void flush();
	int64_t size() const;

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr;
};
