#pragma once

#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string_name.hpp>

#include "component_schema.h"
#include "component_type.h"

namespace vortaris {

// Global registry mapping ComponentTypeId <-> ComponentSchema <-> type name.
//
// Components are registered once, from C++ (via the VECS_REGISTER_COMPONENT
// macro) at module/scene initialization time. Every archetype, serializer and
// network layer queries this registry.
class ComponentRegistry {
public:
	static ComponentRegistry &instance();

	// Registers a schema. Returns the assigned type id.
	ComponentTypeId register_component(const ComponentSchema &p_schema);

	const ComponentSchema *schema_of(ComponentTypeId p_id) const;
	const ComponentSchema *schema_of(const godot::StringName &p_name) const;
	ComponentTypeId id_of(const godot::StringName &p_name) const;
	const godot::StringName &name_of(ComponentTypeId p_id) const;
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
