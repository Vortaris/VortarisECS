#include "component_registry.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string.hpp>

using namespace godot;

namespace vortaris {

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
