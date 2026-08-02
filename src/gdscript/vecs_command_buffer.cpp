#include "vecs_command_buffer.h"

#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_registry.h"
#include "../core/world.h"
#include "../reflect/type_traits.h"
#include "vecs_entity.h"

godot::Ref<VECSCommandBuffer> VECSCommandBuffer::make(vortaris::World *p_world) {
	godot::Ref<VECSCommandBuffer> ref;
	ref.instantiate();
	ref->world_ = p_world;
	return ref;
}

void VECSCommandBuffer::add_component(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_type_name, const godot::Dictionary &p_fields) {
	if (!world_ || !p_entity.is_valid()) {
		return;
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		ERR_PRINT("VortarisECS: component '" + p_type_name + "' is not registered.");
		return;
	}
	const vortaris::ComponentSchema *schema = world_->registry().schema_of(t);
	std::vector<uint8_t> buf(schema->size);
	vortaris::component_dict_to_bytes(*schema, buf.data(), p_fields);
	world_->commands().add_component(p_entity->entity(), t, buf.data(), buf.size());
}

void VECSCommandBuffer::remove_component(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_type_name) {
	if (!world_ || !p_entity.is_valid()) {
		return;
	}
	vortaris::ComponentTypeId t = world_->registry().id_of(godot::StringName(p_type_name));
	if (t != vortaris::INVALID_COMPONENT_TYPE) {
		world_->commands().remove_component(p_entity->entity(), t);
	}
}

void VECSCommandBuffer::add_entity(const godot::Ref<VECSEntity> &p_entity) {
	if (!world_ || !p_entity.is_valid()) {
		return;
	}
	world_->commands().add_entity(p_entity->entity());
}

void VECSCommandBuffer::remove_entity(const godot::Ref<VECSEntity> &p_entity) {
	if (!world_ || !p_entity.is_valid()) {
		return;
	}
	world_->commands().remove_entity(p_entity->entity());
}

void VECSCommandBuffer::flush() {
	if (world_) {
		world_->flush_command_buffers();
	}
}

int64_t VECSCommandBuffer::size() const {
	return world_ ? static_cast<int64_t>(world_->commands().size()) : 0;
}

void VECSCommandBuffer::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("add_component", "entity", "type_name", "fields"), &VECSCommandBuffer::add_component, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("remove_component", "entity", "type_name"), &VECSCommandBuffer::remove_component);
	ClassDB::bind_method(D_METHOD("add_entity", "entity"), &VECSCommandBuffer::add_entity);
	ClassDB::bind_method(D_METHOD("remove_entity", "entity"), &VECSCommandBuffer::remove_entity);
	ClassDB::bind_method(D_METHOD("flush"), &VECSCommandBuffer::flush);
	ClassDB::bind_method(D_METHOD("size"), &VECSCommandBuffer::size);
}
