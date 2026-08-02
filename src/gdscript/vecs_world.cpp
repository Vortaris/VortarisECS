#include "vecs_world.h"

#include <godot_cpp/variant/string.hpp>

#include "../core/entity.h"
#include "../serialization/binary_buffer.h"
#include "vecs_command_buffer.h"
#include "vecs_component_type.h"
#include "vecs_entity.h"
#include "vecs_observer.h"
#include "vecs_query_builder.h"
#include "vecs_system.h"

VECSWorld::VECSWorld() :
		core_(new vortaris::World()),
		scheduler_(new vortaris::SystemScheduler()) {
}

godot::Ref<VECSEntity> VECSWorld::create_entity() {
	vortaris::Entity e = core_->create_entity();
	return VECSEntity::make(core_.get(), e);
}

godot::Ref<VECSEntity> VECSWorld::create_entity_preassigned(int64_t p_id) {
	vortaris::Entity e = core_->create_entity_preassigned(static_cast<uint64_t>(p_id));
	if (!e) {
		return godot::Ref<VECSEntity>();
	}
	return VECSEntity::make(core_.get(), e);
}

void VECSWorld::destroy_entity(const godot::Ref<VECSEntity> &p_entity) {
	if (p_entity.is_valid()) {
		core_->destroy_entity(p_entity->entity());
	}
}

bool VECSWorld::is_alive(const godot::Ref<VECSEntity> &p_entity) const {
	if (!p_entity.is_valid()) {
		return false;
	}
	return core_->is_alive(p_entity->entity());
}

int64_t VECSWorld::entity_count() const {
	return static_cast<int64_t>(core_->entity_count());
}

void VECSWorld::set_entity_range(int64_t p_base) {
	core_->set_entity_range(static_cast<uint32_t>(p_base));
}

godot::Ref<VECSComponentType> VECSWorld::get_component_type(const godot::String &p_name) {
	vortaris::ComponentTypeId t = core_->registry().id_of(godot::StringName(p_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		return godot::Ref<VECSComponentType>();
	}
	return VECSComponentType::make(t);
}

godot::Ref<VECSQueryBuilder> VECSWorld::query() {
	return VECSQueryBuilder::make(core_.get());
}

godot::Ref<VECSCommandBuffer> VECSWorld::commands() {
	return VECSCommandBuffer::make(core_.get());
}

void VECSWorld::add_system(VECSSystem *p_system) {
	if (!p_system) {
		return;
	}
	p_system->set_core_world(core_.get());
	p_system->_setup(*core_);
	scheduler_->add_system(p_system);
}

void VECSWorld::remove_system(VECSSystem *p_system) {
	if (!p_system) {
		return;
	}
	scheduler_->remove_system(p_system);
	p_system->set_core_world(nullptr);
}

int64_t VECSWorld::system_count() const {
	return static_cast<int64_t>(scheduler_->system_count());
}

void VECSWorld::add_observer(VECSObserver *p_observer) {
	if (!p_observer) {
		return;
	}
	vortaris::ObserverCallback cb;
	cb.event_mask = static_cast<uint32_t>(p_observer->get_events());
	cb.component_filter = p_observer->component_filter();
	cb.watch_all = p_observer->component_filter().empty();
	cb.custom_name = p_observer->get_custom_event_name();
	cb.fn = [this, p_observer](vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
		p_observer->handle_event(p_type, p_e, p_comp, p_name, p_payload);
		if ((p_type == vortaris::ObserverEventType::Added || p_type == vortaris::ObserverEventType::Removed) && p_observer->has_match_events()) {
			p_observer->evaluate_match(p_e);
		}
	};
	p_observer->set_world(this);
	p_observer->set_observer_id(core_->observer_dispatch().add(std::move(cb)));
	if (p_observer->has_match_events()) {
		p_observer->seed_membership();
	}
}

void VECSWorld::remove_observer(VECSObserver *p_observer) {
	if (!p_observer) {
		return;
	}
	if (p_observer->observer_id() > 0) {
		core_->observer_dispatch().remove(p_observer->observer_id());
		p_observer->set_observer_id(0);
	}
	p_observer->set_world(nullptr);
}

void VECSWorld::emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload) {
	vortaris::Entity e;
	if (p_entity.is_valid()) {
		e = p_entity->entity();
	}
	core_->emit_event(p_name, e, p_payload);
}

void VECSWorld::process(double p_delta, const godot::String &p_group) {
	// Advance the global write clock so that per-frame component writes get a
	// fresh change tick (required for .changed() queries), then run the group.
	core_->advance_change_tick();
	scheduler_->process(*core_, p_delta, p_group);
}

void VECSWorld::compact() {
	core_->compact();
}

VECSWorld *VECSWorld::get_world() {
	return this;
}

godot::PackedByteArray VECSWorld::serialize_snapshot() const {
	vortaris::BinaryBuffer buf;
	core_->serialize_snapshot(buf);
	return buf.to_packed();
}

bool VECSWorld::deserialize_snapshot(const godot::PackedByteArray &p_data) {
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_data);
	return core_->deserialize_snapshot(buf);
}

void VECSWorld::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("create_entity"), &VECSWorld::create_entity);
	ClassDB::bind_method(D_METHOD("create_entity_preassigned", "id"), &VECSWorld::create_entity_preassigned);
	ClassDB::bind_method(D_METHOD("destroy_entity", "entity"), &VECSWorld::destroy_entity);
	ClassDB::bind_method(D_METHOD("is_alive", "entity"), &VECSWorld::is_alive);
	ClassDB::bind_method(D_METHOD("entity_count"), &VECSWorld::entity_count);
	ClassDB::bind_method(D_METHOD("set_entity_range", "base"), &VECSWorld::set_entity_range);
	ClassDB::bind_method(D_METHOD("get_component_type", "name"), &VECSWorld::get_component_type);
	ClassDB::bind_method(D_METHOD("query"), &VECSWorld::query);
	ClassDB::bind_method(D_METHOD("commands"), &VECSWorld::commands);
	ClassDB::bind_method(D_METHOD("add_system", "system"), &VECSWorld::add_system);
	ClassDB::bind_method(D_METHOD("remove_system", "system"), &VECSWorld::remove_system);
	ClassDB::bind_method(D_METHOD("system_count"), &VECSWorld::system_count);
	ClassDB::bind_method(D_METHOD("add_observer", "observer"), &VECSWorld::add_observer);
	ClassDB::bind_method(D_METHOD("remove_observer", "observer"), &VECSWorld::remove_observer);
	ClassDB::bind_method(D_METHOD("emit_event", "name", "entity", "payload"), &VECSWorld::emit_event, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("process", "delta", "group"), &VECSWorld::process, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("compact"), &VECSWorld::compact);
	ClassDB::bind_method(D_METHOD("get_world"), &VECSWorld::get_world);
	ClassDB::bind_method(D_METHOD("serialize_snapshot"), &VECSWorld::serialize_snapshot);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot", "data"), &VECSWorld::deserialize_snapshot);
}
