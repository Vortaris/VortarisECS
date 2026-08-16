#include "vecs_component.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"
#include "../core/world.h"
#include "../reflect/type_traits.h"
#include "vecs_log.h"

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

godot::Variant VECSComponent::get_field(const godot::String &p_name, const godot::Variant &p_default) const {
	if (!is_valid()) {
		return p_default;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return p_default;
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd) {
		return p_default;
	}
	const void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return p_default;
	}
	godot::Variant out;
	if (!vortaris::field_to_variant(*fd, static_cast<const uint8_t *>(raw) + fd->offset, out)) {
		return p_default;
	}
	return out;
}

int64_t VECSComponent::get_field_count(const godot::String &p_name) const {
	if (!is_valid()) {
		return 0;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return 0;
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	return fd ? static_cast<int64_t>(fd->count) : 0;
}

godot::Variant VECSComponent::get_array_element(const godot::String &p_name, int64_t p_index) const {
	if (!is_valid() || p_index < 0) {
		return godot::Variant();
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return godot::Variant();
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd || fd->count <= 1 || fd->type == vortaris::FieldType::StringFixed || fd->type == vortaris::FieldType::Blob) {
		return godot::Variant();
	}
	if (static_cast<size_t>(p_index) >= fd->count) {
		return godot::Variant();
	}
	const void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return godot::Variant();
	}
	const size_t elem_size = fd->storage_size() / fd->count;
	const uint8_t *ptr = static_cast<const uint8_t *>(raw) + fd->offset + static_cast<size_t>(p_index) * elem_size;
	godot::Variant out;
	if (!vortaris::element_to_variant(static_cast<vortaris::FieldType>(fd->element_type), ptr, out)) {
		return godot::Variant();
	}
	return out;
}

bool VECSComponent::field_contains(const godot::String &p_name, const godot::Variant &p_value) const {
	if (!is_valid()) {
		return false;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return false;
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd) {
		return false;
	}
	const void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return false;
	}
	if (fd->count <= 1 || fd->type == vortaris::FieldType::StringFixed || fd->type == vortaris::FieldType::Blob) {
		// Scalar (or blob/string buffer): compare the single value.
		godot::Variant v;
		if (!vortaris::field_to_variant(*fd, static_cast<const uint8_t *>(raw) + fd->offset, v)) {
			return false;
		}
		return vortaris::variants_equal(v, p_value);
	}
	// Fixed array: any element equal to p_value.
	const size_t elem_size = fd->storage_size() / fd->count;
	const uint8_t *ptr = static_cast<const uint8_t *>(raw) + fd->offset;
	for (size_t i = 0; i < fd->count; ++i) {
		godot::Variant v;
		if (vortaris::element_to_variant(static_cast<vortaris::FieldType>(fd->element_type), ptr + i * elem_size, v) && vortaris::variants_equal(v, p_value)) {
			return true;
		}
	}
	return false;
}

bool VECSComponent::set_array_element(const godot::String &p_name, int64_t p_index, const godot::Variant &p_value) {
	if (!is_valid() || p_index < 0) {
		return false;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(type_id_);
	if (!schema) {
		return false;
	}
	const vortaris::FieldDescriptor *fd = schema->find_field(godot::StringName(p_name));
	if (!fd || fd->count <= 1 || fd->type == vortaris::FieldType::StringFixed || fd->type == vortaris::FieldType::Blob) {
		return false;
	}
	if (static_cast<size_t>(p_index) >= fd->count) {
		return false;
	}
	void *raw = world_->get_raw(entity_, type_id_);
	if (!raw) {
		return false;
	}
	const size_t elem_size = fd->storage_size() / fd->count;
	uint8_t *ptr = static_cast<uint8_t *>(raw) + fd->offset + static_cast<size_t>(p_index) * elem_size;
	if (!vortaris::element_from_variant(static_cast<vortaris::FieldType>(fd->element_type), ptr, p_value)) {
		return false;
	}
	world_->mark_changed(entity_, type_id_, p_name);
	return true;
}

int64_t VECSComponent::get_int(const godot::String &p_name) const {
	const godot::Variant v = get_field(p_name);
	switch (v.get_type()) {
		case godot::Variant::INT:
			return static_cast<int64_t>(v);
		case godot::Variant::FLOAT:
			return static_cast<int64_t>(static_cast<double>(v));
		case godot::Variant::BOOL:
			return static_cast<bool>(v) ? 1 : 0;
		default:
			return 0;
	}
}

double VECSComponent::get_float(const godot::String &p_name) const {
	const godot::Variant v = get_field(p_name);
	switch (v.get_type()) {
		case godot::Variant::FLOAT:
			return static_cast<double>(v);
		case godot::Variant::INT:
			return static_cast<double>(static_cast<int64_t>(v));
		case godot::Variant::BOOL:
			return static_cast<bool>(v) ? 1.0 : 0.0;
		default:
			return 0.0;
	}
}

bool VECSComponent::get_bool(const godot::String &p_name) const {
	const godot::Variant v = get_field(p_name);
	switch (v.get_type()) {
		case godot::Variant::BOOL:
			return static_cast<bool>(v);
		case godot::Variant::INT:
			return static_cast<int64_t>(v) != 0;
		case godot::Variant::FLOAT:
			return static_cast<double>(v) != 0.0;
		default:
			return false;
	}
}

godot::String VECSComponent::get_string(const godot::String &p_name) const {
	const godot::Variant v = get_field(p_name);
	if (v.get_type() == godot::Variant::NIL) {
		return godot::String();
	}
	return v.stringify();
}

godot::Variant VECSComponent::get_vector(const godot::String &p_name) const {
	const godot::Variant v = get_field(p_name);
	switch (v.get_type()) {
		case godot::Variant::VECTOR2:
		case godot::Variant::VECTOR2I:
		case godot::Variant::VECTOR3:
		case godot::Variant::VECTOR3I:
		case godot::Variant::VECTOR4:
		case godot::Variant::VECTOR4I:
			return v;
		default:
			return godot::Variant();
	}
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
		// Field-level change notification: the Changed event carries the field
		// name so observers can subscribe per-field.
		world_->mark_changed(entity_, type_id_, p_name);
		if (vortaris::verbose_active()) {
			vortaris::log_verbose("component write entity=" + godot::String::num_int64(static_cast<int64_t>(entity_.id)) +
					" type=" + get_type_name() + " field=" + p_name);
		}
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
	ClassDB::bind_method(D_METHOD("get_field", "name", "default"), &VECSComponent::get_field, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_field", "name", "value"), &VECSComponent::set_field);
	ClassDB::bind_method(D_METHOD("get_fields"), &VECSComponent::get_fields);
	ClassDB::bind_method(D_METHOD("get_field_count", "name"), &VECSComponent::get_field_count);
	ClassDB::bind_method(D_METHOD("get_array_element", "name", "index"), &VECSComponent::get_array_element);
	ClassDB::bind_method(D_METHOD("set_array_element", "name", "index", "value"), &VECSComponent::set_array_element);
	ClassDB::bind_method(D_METHOD("field_contains", "name", "value"), &VECSComponent::field_contains);
	ClassDB::bind_method(D_METHOD("get_int", "name"), &VECSComponent::get_int);
	ClassDB::bind_method(D_METHOD("get_float", "name"), &VECSComponent::get_float);
	ClassDB::bind_method(D_METHOD("get_bool", "name"), &VECSComponent::get_bool);
	ClassDB::bind_method(D_METHOD("get_string", "name"), &VECSComponent::get_string);
	ClassDB::bind_method(D_METHOD("get_vector", "name"), &VECSComponent::get_vector);
}
