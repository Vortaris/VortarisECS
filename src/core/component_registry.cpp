#include "component_registry.h"

#include <algorithm>

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace vortaris {

namespace {

size_t align_up(size_t p_v, size_t p_a) {
	return (p_v + p_a - 1) / p_a * p_a;
}

// Size in bytes of one scalar element of a field type (0 for StringFixed/Blob,
// whose size is carried by `count`).
size_t field_type_size(FieldType p_t) {
	switch (p_t) {
		case FieldType::Bool:
		case FieldType::I8:
		case FieldType::U8:
			return 1;
		case FieldType::I16:
		case FieldType::U16:
			return 2;
		case FieldType::I32:
		case FieldType::U32:
		case FieldType::F32:
			return 4;
		case FieldType::I64:
		case FieldType::U64:
		case FieldType::F64:
			return 8;
		case FieldType::Vector2:
		case FieldType::Vector2i:
			return 8;
		case FieldType::Vector3:
		case FieldType::Vector3i:
			return 12;
		case FieldType::Vector4:
		case FieldType::Vector4i:
			return 16;
		case FieldType::Color:
			return 16;
		case FieldType::Quaternion:
			return 16;
		case FieldType::Basis:
			return 36;
		case FieldType::Transform2D:
			return 24;
		case FieldType::Transform3D:
			return 64;
		case FieldType::AABB:
			return 24;
		case FieldType::Rect2:
			return 16;
		case FieldType::Plane:
			return 16;
		case FieldType::StringFixed:
		case FieldType::Blob:
			return 0;
	}
	return 0;
}

size_t field_type_align(FieldType p_t) {
	switch (p_t) {
		case FieldType::Bool:
		case FieldType::I8:
		case FieldType::U8:
			return 1;
		case FieldType::I16:
		case FieldType::U16:
			return 2;
		case FieldType::I64:
		case FieldType::U64:
		case FieldType::F64:
			return 8;
		case FieldType::Vector4:
		case FieldType::Vector4i:
			return 16;
		default:
			return 4;
	}
}

} // namespace

ComponentRegistry &ComponentRegistry::instance() {
	static ComponentRegistry registry;
	return registry;
}

ComponentTypeId ComponentRegistry::register_component(const ComponentSchema &p_schema) {
	ERR_FAIL_COND_V_MSG(p_schema.type_name == StringName(), INVALID_COMPONENT_TYPE,
			"VortarisECS: cannot register a component with an empty type name.");
	ERR_FAIL_COND_V_MSG(id_by_name_.count(p_schema.type_name) > 0, id_by_name_[p_schema.type_name],
			"VortarisECS: component '" + String(p_schema.type_name) + "' is already registered.");

	ComponentTypeId id = static_cast<ComponentTypeId>(schemas_.size() + 1);
	ComponentSchema schema = p_schema;
	schema.type_id = id;
	schemas_.push_back(schema);
	id_by_name_[schema.type_name] = id;
	return id;
}

ComponentTypeId ComponentRegistry::register_schema_component(const godot::StringName &p_name, const std::vector<FieldDescriptor> &p_raw_fields) {
	std::vector<FieldDescriptor> fields = p_raw_fields;
	size_t offset = 0;
	size_t max_align = 1;
	for (FieldDescriptor &fd : fields) {
		size_t elem_size = 0;
		size_t elem_align = 1;
		if (fd.type == FieldType::StringFixed || fd.type == FieldType::Blob) {
			elem_size = fd.count; // count carries the byte length
			elem_align = 1;
			fd.element_type = 0;
			fd.byte_size = fd.count;
		} else {
			elem_size = field_type_size(fd.type);
			elem_align = field_type_align(fd.type);
			if (elem_size == 0) {
				ERR_PRINT("VortarisECS: unsupported field type for component '" + String(p_name) + "'.");
				return INVALID_COMPONENT_TYPE;
			}
			fd.element_type = fd.count > 1 ? static_cast<uint8_t>(fd.type) : 0;
			fd.byte_size = elem_size * (fd.count > 0 ? fd.count : 1);
		}
		fd.offset = align_up(offset, elem_align);
		offset = fd.offset + fd.byte_size;
		max_align = std::max(max_align, elem_align);
	}

	ComponentSchema schema;
	schema.type_name = p_name;
	schema.size = align_up(offset, max_align);
	schema.alignment = max_align;
	schema.fields = std::move(fields);
	for (const FieldDescriptor &f : schema.fields) {
		if (f.is_networked) {
			schema.is_networked = true;
		}
	}
	return register_component(schema);
}

bool ComponentRegistry::parse_field_type(const godot::String &p_str, FieldType &r_out) {
	const String &s = p_str;
	if (s == "Bool") {
		r_out = FieldType::Bool;
	} else if (s == "I8") {
		r_out = FieldType::I8;
	} else if (s == "I16") {
		r_out = FieldType::I16;
	} else if (s == "I32") {
		r_out = FieldType::I32;
	} else if (s == "I64") {
		r_out = FieldType::I64;
	} else if (s == "U8") {
		r_out = FieldType::U8;
	} else if (s == "U16") {
		r_out = FieldType::U16;
	} else if (s == "U32") {
		r_out = FieldType::U32;
	} else if (s == "U64") {
		r_out = FieldType::U64;
	} else if (s == "F32") {
		r_out = FieldType::F32;
	} else if (s == "F64") {
		r_out = FieldType::F64;
	} else if (s == "Vector2") {
		r_out = FieldType::Vector2;
	} else if (s == "Vector2i") {
		r_out = FieldType::Vector2i;
	} else if (s == "Vector3") {
		r_out = FieldType::Vector3;
	} else if (s == "Vector3i") {
		r_out = FieldType::Vector3i;
	} else if (s == "Vector4") {
		r_out = FieldType::Vector4;
	} else if (s == "Vector4i") {
		r_out = FieldType::Vector4i;
	} else if (s == "Color") {
		r_out = FieldType::Color;
	} else if (s == "Quaternion") {
		r_out = FieldType::Quaternion;
	} else if (s == "Basis") {
		r_out = FieldType::Basis;
	} else if (s == "Transform2D") {
		r_out = FieldType::Transform2D;
	} else if (s == "Transform3D") {
		r_out = FieldType::Transform3D;
	} else if (s == "AABB") {
		r_out = FieldType::AABB;
	} else if (s == "Rect2") {
		r_out = FieldType::Rect2;
	} else if (s == "Plane") {
		r_out = FieldType::Plane;
	} else if (s == "StringFixed") {
		r_out = FieldType::StringFixed;
	} else if (s == "Blob") {
		r_out = FieldType::Blob;
	} else {
		return false;
	}
	return true;
}

const ComponentSchema *ComponentRegistry::schema_of(ComponentTypeId p_id) const {
	if (p_id == INVALID_COMPONENT_TYPE || p_id > schemas_.size()) {
		return nullptr;
	}
	return &schemas_[p_id - 1];
}

const ComponentSchema *ComponentRegistry::schema_of(const godot::StringName &p_name) const {
	auto it = id_by_name_.find(p_name);
	if (it == id_by_name_.end()) {
		return nullptr;
	}
	return schema_of(it->second);
}

ComponentTypeId ComponentRegistry::id_of(const godot::StringName &p_name) const {
	auto it = id_by_name_.find(p_name);
	return it == id_by_name_.end() ? INVALID_COMPONENT_TYPE : it->second;
}

const godot::StringName &ComponentRegistry::name_of(ComponentTypeId p_id) const {
	const ComponentSchema *schema = schema_of(p_id);
	static const StringName empty;
	return schema ? schema->type_name : empty;
}

void ComponentRegistry::clear() {
	schemas_.clear();
	id_by_name_.clear();
	// NOTE: TypeIdHolder<T>::id is intentionally NOT reset here; clearing is
	// only performed at extension teardown when the process is about to exit.
}

} // namespace vortaris
