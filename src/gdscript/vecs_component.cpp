#include "vecs_component.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"
#include "../core/world.h"
#include "../reflect/type_traits.h"

godot::Ref<VECSComponent> VECSComponent::make(vortaris::World *p_world, vortaris::Entity p_entity, vortaris::ComponentTypeId p_type) {
	godot::Ref<VECSComponent> ref;
	ref.instantiate();
	ref->world_ = p_world;
	ref->entity_ = p_entity;
	ref->type_id_ = p_type;
	return ref;
}

bool VECSComponent::is_valid() const {
	return world_ != nullptr && world_->is_alive(entity_) && world_->has(entity_, type_id_);
}

godot::String VECSComponent::get_type_name() const {
	if (!world_) {
		return godot::String();
	}
	return godot::String(world_->registry().name_of(type_id_));
}

godot::Variant VECSComponent::get_field(const godot::String &p_name) const {
	if (!is_valid()) {
		return godot::Variant();
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return godot::Variant();
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd) {
		ERR_PRINT("VortarisECS: component '" + get_type_name() + "' has no field '" + p_name + "'.");
		return godot::Variant();
	}
	const void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return godot::Variant();
	}
	godot::Variant out;
	vortaris::field_to_variant(*fd, static_cast<const uint8_t *>(raw) + fd->offset, out);
	return out;
}

void VECSComponent::set_field(const godot::String &p_name, const godot::Variant &p_value) {
	if (!is_valid()) {
		return;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return;
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd) {
		ERR_PRINT("VortarisECS: component '" + get_type_name() + "' has no field '" + p_name + "'.");
		return;
	}
	void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return;
	}
	if (vortaris::field_from_variant(*fd, static_cast<uint8_t *>(raw) + fd->offset, p_value)) {
		world_->mark_changed(entity_, type_id_);
	} else {
		ERR_PRINT("VortarisECS: set_field on component '" + get_type_name() + "' field '" + p_name + "' received an incompatible Variant type.");
	}
}

godot::Dictionary VECSComponent::get_fields() const {
	godot::Dictionary out;
	if (!is_valid()) {
		return out;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return out;
	}
	const void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return out;
	}
	vortaris::component_bytes_to_variant_dict(*schema, raw, out);
	return out;
}

void VECSComponent::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("is_valid"), &VECSComponent::is_valid);
	ClassDB::bind_method(D_METHOD("get_type_name"), &VECSComponent::get_type_name);
	ClassDB::bind_method(D_METHOD("get_field", "name"), &VECSComponent::get_field);
	ClassDB::bind_method(D_METHOD("set_field", "name", "value"), &VECSComponent::set_field);
	ClassDB::bind_method(D_METHOD("get_fields"), &VECSComponent::get_fields);
}
