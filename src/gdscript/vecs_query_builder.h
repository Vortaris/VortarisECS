#pragma once

#include <vector>

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>

#include "../core/component_type.h"
#include "../core/entity.h"

namespace vortaris {
class World;
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

	godot::Array execute();
	godot::Ref<VECSEntity> execute_one();
	int64_t count();

protected:
	static void _bind_methods();

private:
	vortaris::World *world_ = nullptr;
	std::vector<vortaris::ComponentTypeId> all_;
	std::vector<vortaris::ComponentTypeId> any_;
	std::vector<vortaris::ComponentTypeId> none_;
	std::vector<vortaris::ComponentTypeId> changed_;
	bool enabled_only_ = false;

	std::vector<vortaris::ComponentTypeId> resolve(const godot::Array &p_names) const;
};
