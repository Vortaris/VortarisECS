#include "vecs_entity.h"

#include <functional>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_schema.h"
#include "../core/world.h"
#include "../reflect/type_traits.h"
#include "vecs_component.h"

godot::Ref<VECSEntity> VECSEntity::make(vortaris::World *p_world, vortaris::Entity p_entity) {
	godot::Ref<VECSEntity> ref;
	ref.instantiate();
	ref->world_ = p_world;
	ref->id_ = p_entity.id;
	return ref;
}

bool VECSEntity::is_alive() const {
	return world_ != nullptr && world_->is_alive(entity());
}

int64_t VECSEntity::get_id() const {
	return static_cast<int64_t>(id_);
}

bool VECSEntity::has_component(const godot::String &p_type_name) const {
	if (!is_alive()) {
		return false;
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	return t != vortaris::INVALID_COMPONENT_TYPE && world_->has(entity(), t);
}

godot::Array VECSEntity::get_component_types() const {
	godot::Array out;
	if (!world_ || !world_->is_alive(entity())) {
		return out;
	}
	std::vector<vortaris::ComponentTypeId> types;
	world_->get_entity_component_types(entity(), types);
	for (vortaris::ComponentTypeId t : types) {
		out.append(godot::String(vortaris::ComponentRegistry::instance().name_of(t)));
	}
	return out;
}

godot::Ref<VECSComponent> VECSEntity::get_component(const godot::String &p_type_name) const {
	if (!is_alive()) {
		return godot::Ref<VECSComponent>();
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		return godot::Ref<VECSComponent>();
	}
	return VECSComponent::make(world_, entity(), t);
}

void VECSEntity::add_component(const godot::String &p_type_name, const godot::Dictionary &p_fields) {
	if (!world_) {
		return;
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		ERR_PRINT("VortarisECS: component '" + p_type_name + "' is not registered.");
		return;
	}
	if (!world_->is_alive(entity())) {
		return;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(t);
	std::vector<uint8_t> buf(schema->size);
	vortaris::component_dict_to_bytes(*schema, buf.data(), p_fields);
	world_->add_raw(entity(), t, buf.data());
}

void VECSEntity::remove_component(const godot::String &p_type_name) {
	if (!world_) {
		return;
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		return;
	}
	world_->remove_component(entity(), t);
}

bool VECSEntity::equals(const godot::Ref<VECSEntity> &p_other) const {
	return p_other.is_valid() && id_ == p_other->id_;
}

int64_t VECSEntity::hash_value() const {
	return static_cast<int64_t>(std::hash<uint64_t>{}(id_));
}

void VECSEntity::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("is_alive"), &VECSEntity::is_alive);
	ClassDB::bind_method(D_METHOD("get_id"), &VECSEntity::get_id);
	ClassDB::bind_method(D_METHOD("has_component", "type_name"), &VECSEntity::has_component);
	ClassDB::bind_method(D_METHOD("get_component_types"), &VECSEntity::get_component_types);
	ClassDB::bind_method(D_METHOD("get_component", "type_name"), &VECSEntity::get_component);
	ClassDB::bind_method(D_METHOD("add_component", "type_name", "fields"), &VECSEntity::add_component, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_component", "type_name"), &VECSEntity::remove_component);
	ClassDB::bind_method(D_METHOD("equals", "other"), &VECSEntity::equals);
	ClassDB::bind_method(D_METHOD("hash_value"), &VECSEntity::hash_value);
}
