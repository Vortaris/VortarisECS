#include "vecs_component_type.h"

#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"

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
	ClassDB::bind_method(D_METHOD("get_field_sync_priority", "field"), &VECSComponentType::get_field_sync_priority);
	ClassDB::bind_method(D_METHOD("get_field_is_networked", "field"), &VECSComponentType::get_field_is_networked);
}
