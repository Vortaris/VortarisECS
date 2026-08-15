#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "component_schema.h"
#include "component_type.h"

namespace vortaris {

// Global registry mapping ComponentTypeId <-> ComponentSchema <-> type name.
//
// Components are registered either from C++ (via the VECS_REGISTER_COMPONENT
// macro, using a concrete trivially-copyable struct) or from a script (via
// register_schema_component, with no C++ type — the layout is computed from
// the field descriptors). Every archetype, serializer and network layer
// queries this registry, so both kinds behave identically.
class ComponentRegistry {
public:
	static ComponentRegistry &instance();

	// Registers a schema. Returns the assigned type id.
	ComponentTypeId register_component(const ComponentSchema &p_schema);

	// Registers a schema-only component (no C++ type) from raw field
	// descriptors. byte_size / offset / alignment are computed here.
	// Returns INVALID_COMPONENT_TYPE on failure (bad type / duplicate name).
	ComponentTypeId register_schema_component(const godot::StringName &p_name, const std::vector<FieldDescriptor> &p_raw_fields);

	// Maps a field type name ("F32", "Vector3", "Blob", ...) to FieldType.
	static bool parse_field_type(const godot::String &p_str, FieldType &r_out);

	const ComponentSchema *schema_of(ComponentTypeId p_id) const;
	const ComponentSchema *schema_of(const godot::StringName &p_name) const;
	ComponentTypeId id_of(const godot::StringName &p_name) const;
	godot::StringName name_of(ComponentTypeId p_id) const;
	size_t count() const { return schemas_.size(); }

	void clear();

private:
	ComponentRegistry() = default;

	std::vector<ComponentSchema> schemas_; // indexed by (type_id - 1)
	std::unordered_map<godot::StringName, ComponentTypeId> id_by_name_;
};

// Per-type static id holder. Each registered C++ component type T gets a
// unique ComponentTypeId here, enabling world.get<T>() / for_each<T>.
template <class T>
struct TypeIdHolder {
	static ComponentTypeId id;
};
template <class T>
ComponentTypeId TypeIdHolder<T>::id = INVALID_COMPONENT_TYPE;

template <class T>
ComponentTypeId type_id_of() {
	return TypeIdHolder<T>::id;
}

// Template registration helper used by the VECS_REGISTER_COMPONENT macro.
template <class T>
ComponentTypeId register_component_type(const godot::StringName &p_name, const std::vector<FieldDescriptor> &p_fields) {
	if (TypeIdHolder<T>::id != INVALID_COMPONENT_TYPE) {
		return TypeIdHolder<T>::id; // already registered
	}
	ComponentSchema schema;
	schema.type_name = p_name;
	schema.size = sizeof(T);
	schema.alignment = alignof(T);
	schema.fields = p_fields;
	for (const auto &f : p_fields) {
		if (f.is_networked) {
			schema.is_networked = true;
		}
	}
	TypeIdHolder<T>::id = ComponentRegistry::instance().register_component(schema);
	return TypeIdHolder<T>::id;
}

} // namespace vortaris
