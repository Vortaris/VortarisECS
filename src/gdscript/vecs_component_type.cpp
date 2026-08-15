#include "vecs_component_type.h"

#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"

namespace {
const char *field_type_name(vortaris::FieldType p_t) {
	switch (p_t) {
		case vortaris::FieldType::Bool: return "Bool";
		case vortaris::FieldType::I8: return "I8";
		case vortaris::FieldType::I16: return "I16";
		case vortaris::FieldType::I32: return "I32";
		case vortaris::FieldType::I64: return "I64";
		case vortaris::FieldType::U8: return "U8";
		case vortaris::FieldType::U16: return "U16";
		case vortaris::FieldType::U32: return "U32";
		case vortaris::FieldType::U64: return "U64";
		case vortaris::FieldType::F32: return "F32";
		case vortaris::FieldType::F64: return "F64";
		case vortaris::FieldType::Vector2: return "Vector2";
		case vortaris::FieldType::Vector2i: return "Vector2i";
		case vortaris::FieldType::Vector3: return "Vector3";
		case vortaris::FieldType::Vector3i: return "Vector3i";
		case vortaris::FieldType::Vector4: return "Vector4";
		case vortaris::FieldType::Vector4i: return "Vector4i";
		case vortaris::FieldType::Color: return "Color";
		case vortaris::FieldType::Quaternion: return "Quaternion";
		case vortaris::FieldType::Basis: return "Basis";
		case vortaris::FieldType::Transform2D: return "Transform2D";
		case vortaris::FieldType::Transform3D: return "Transform3D";
		case vortaris::FieldType::AABB: return "AABB";
		case vortaris::FieldType::Rect2: return "Rect2";
		case vortaris::FieldType::Plane: return "Plane";
		case vortaris::FieldType::StringFixed: return "StringFixed";
		case vortaris::FieldType::Blob: return "Blob";
	}
	return "";
}
} // namespace

godot::Ref<VECSComponentType> VECSComponentType::make(vortaris::ComponentTypeId p_type) {
	godot::Ref<VECSComponentType> ref;
	ref.instantiate();
	ref->type_id_ = p_type;
	return ref;
}

godot::String VECSComponentType::get_name() const {
	return godot::String(vortaris::ComponentRegistry::instance().name_of(type_id_));
}

int64_t VECSComponentType::get_id() const {
	return static_cast<int64_t>(type_id_);
}

int64_t VECSComponentType::get_size() const {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	return s ? static_cast<int64_t>(s->size) : 0;
}

godot::Array VECSComponentType::get_field_names() const {
	godot::Array out;
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	if (!s) {
		return out;
	}
	for (const auto &f : s->fields) {
		out.append(godot::String(f.name));
	}
	return out;
}

int64_t VECSComponentType::get_field_count(const godot::String &p_field) const {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	if (!s) {
		return 0;
	}
	const vortaris::FieldDescriptor *fd = s->find_field(godot::StringName(p_field));
	return fd ? static_cast<int64_t>(fd->count) : 0;
}

godot::String VECSComponentType::get_field_type(const godot::String &p_field) const {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	if (!s) {
		return godot::String();
	}
	const vortaris::FieldDescriptor *fd = s->find_field(godot::StringName(p_field));
	if (!fd) {
		return godot::String();
	}
	const godot::String base(field_type_name(fd->type));
	if (fd->count > 1) {
		return godot::String("Array:") + base;
	}
	return base;
}

int64_t VECSComponentType::get_field_sync_priority(const godot::String &p_field) const {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	if (!s) {
		return -1;
	}
	const vortaris::FieldDescriptor *fd = s->find_field(godot::StringName(p_field));
	return fd ? static_cast<int64_t>(fd->sync_priority) : -1;
}

bool VECSComponentType::get_field_is_networked(const godot::String &p_field) const {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(type_id_);
	if (!s) {
		return false;
	}
	const vortaris::FieldDescriptor *fd = s->find_field(godot::StringName(p_field));
	return fd ? fd->is_networked : false;
}

void VECSComponentType::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("get_name"), &VECSComponentType::get_name);
	ClassDB::bind_method(D_METHOD("get_id"), &VECSComponentType::get_id);
	ClassDB::bind_method(D_METHOD("get_size"), &VECSComponentType::get_size);
	ClassDB::bind_method(D_METHOD("get_field_names"), &VECSComponentType::get_field_names);
	ClassDB::bind_method(D_METHOD("get_field_count", "field"), &VECSComponentType::get_field_count);
	ClassDB::bind_method(D_METHOD("get_field_type", "field"), &VECSComponentType::get_field_type);
	ClassDB::bind_method(D_METHOD("get_field_sync_priority", "field"), &VECSComponentType::get_field_sync_priority);
	ClassDB::bind_method(D_METHOD("get_field_is_networked", "field"), &VECSComponentType::get_field_is_networked);
}
