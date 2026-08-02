#include "vecs_system.h"

#include <godot_cpp/core/error_macros.hpp>

#include "../core/world.h"

VARIANT_ENUM_CAST(VECSSystem::FlushMode);

vortaris::CommandBuffer &VECSSystem::cmd() {
	static vortaris::CommandBuffer dummy;
	ERR_FAIL_NULL_V(core_, dummy);
	return core_->commands();
}

void VECSSystem::handle(double p_delta) {
	if (!core_) {
		return;
	}
	if (_gdvirtual__script_process_overridden()) {
		_gdvirtual__script_process_call(p_delta);
	} else {
		_run(*core_, p_delta);
	}
}

void VECSSystem::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("get_system_name"), &VECSSystem::get_system_name);
	ClassDB::bind_method(D_METHOD("handle", "delta"), &VECSSystem::handle);
	ClassDB::bind_method(D_METHOD("set_group", "value"), &VECSSystem::set_group);
	ClassDB::bind_method(D_METHOD("get_group"), &VECSSystem::get_group);
	ClassDB::bind_method(D_METHOD("set_active", "value"), &VECSSystem::set_active);
	ClassDB::bind_method(D_METHOD("get_active"), &VECSSystem::get_active);
	ClassDB::bind_method(D_METHOD("set_paused", "value"), &VECSSystem::set_paused);
	ClassDB::bind_method(D_METHOD("get_paused"), &VECSSystem::get_paused);
	ClassDB::bind_method(D_METHOD("set_flush_mode", "value"), &VECSSystem::set_flush_mode);
	ClassDB::bind_method(D_METHOD("get_flush_mode"), &VECSSystem::get_flush_mode);
	ClassDB::bind_method(D_METHOD("set_tick_interval", "value"), &VECSSystem::set_tick_interval);
	ClassDB::bind_method(D_METHOD("get_tick_interval"), &VECSSystem::get_tick_interval);
	ClassDB::add_property("VECSSystem", PropertyInfo(Variant::STRING, "group"), "set_group", "get_group");
	ClassDB::add_property("VECSSystem", PropertyInfo(Variant::BOOL, "active"), "set_active", "get_active");
	ClassDB::add_property("VECSSystem", PropertyInfo(Variant::BOOL, "paused"), "set_paused", "get_paused");
	ClassDB::add_property("VECSSystem", PropertyInfo(Variant::INT, "flush_mode"), "set_flush_mode", "get_flush_mode");
	ClassDB::add_property("VECSSystem", PropertyInfo(Variant::FLOAT, "tick_interval"), "set_tick_interval", "get_tick_interval");
	GDVIRTUAL_BIND(_script_process, "delta");
	BIND_ENUM_CONSTANT(PER_SYSTEM);
	BIND_ENUM_CONSTANT(PER_GROUP);
	BIND_ENUM_CONSTANT(MANUAL);
}
