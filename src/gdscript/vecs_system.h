#pragma once

#include <vector>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/string.hpp>

#include "../core/command_buffer.h"
#include "../core/entity.h"
#include "../core/system_types.h"

namespace vortaris {
class World;
}

class VECSWorld;

// Base class for ECS systems.
//
// C++ systems override the virtuals below and are driven by the scheduler
// (VECSWorld::add_system + VECSWorld::process). GDScript subclasses may
// override `_script_process(delta)` instead; they get access to the world
// through the VECSQueryBuilder / VECSEntity script API (not the C++ templates).
class VECSSystem : public godot::Node {
	GDCLASS(VECSSystem, godot::Node)

public:
	enum FlushMode {
		PER_SYSTEM = 0,
		PER_GROUP = 1,
		MANUAL = 2,
	};

	// --- C++ override points ---
	virtual void _setup(vortaris::World &p_world) {}
	virtual void _run(vortaris::World &p_world, double p_delta) {}
	virtual void _deps(std::vector<vortaris::SystemDep> &r_deps) {}

	// --- GDScript override point ---
	GDVIRTUAL1(_script_process, double);

	// --- config properties (GDScript-exposed) ---
	void set_group(const godot::String &p_v) { group = p_v; }
	godot::String get_group() const { return group; }
	void set_active(bool p_v) { active = p_v; }
	bool get_active() const { return active; }
	void set_paused(bool p_v) { paused = p_v; }
	bool get_paused() const { return paused; }
	void set_flush_mode(int p_v) { flush_mode = p_v; }
	int get_flush_mode() const { return flush_mode; }
	void set_tick_interval(double p_v) { tick_interval = p_v; }
	double get_tick_interval() const { return tick_interval; }

	godot::String get_system_name() const { return get_name(); }

	// --- world access (injected by the scheduler) ---
	vortaris::World *core_world() const { return core_; }
	void set_core_world(vortaris::World *p_world) { core_ = p_world; }
	// Script-facing handle to the owning VECSWorld (null for C++-only systems
	// that never needed a node). Lets GDScript systems reach the script API.
	void set_world_node(VECSWorld *p_world) { world_node_ = p_world; }
	VECSWorld *get_world_node() const { return world_node_; }
	vortaris::CommandBuffer &cmd();

	// Scheduler entry: dispatches to the script override or the C++ virtual.
	void handle(double p_delta);

protected:
	static void _bind_methods();

private:
	vortaris::World *core_ = nullptr;
	VECSWorld *world_node_ = nullptr;
	godot::String group = "";
	bool active = true;
	bool paused = false;
	int flush_mode = PER_SYSTEM;
	double tick_interval = 0.0;
};
