#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include "../core/component_schema.h" // std::hash<godot::StringName>
#include "../core/system_types.h"

class VECSSystem;

namespace vortaris {

class World;

// Group-based system scheduler with dependency ordering, per-system timers and
// command-buffer flush modes. Systems are grouped by the `group` property;
// VECSWorld::process(delta, group) drives the matching group every frame.
class SystemScheduler {
public:
	void add_system(VECSSystem *p_sys);
	void remove_system(VECSSystem *p_sys);
	void process(World &p_world, double p_delta, const godot::String &p_group);
	void clear();
	size_t system_count() const;

private:
	void recompute_order(const godot::StringName &p_group);
	std::vector<VECSSystem *> &ordered(const godot::StringName &p_group);

	std::unordered_map<godot::StringName, std::vector<VECSSystem *>> systems_by_group_;
	std::unordered_map<godot::StringName, std::vector<VECSSystem *>> order_cache_;
	std::unordered_map<VECSSystem *, double> next_run_;
	// Per-group accumulated time so a system's tick_interval is not distorted by
	// other groups sharing the frame.
	std::unordered_map<godot::StringName, double> elapsed_by_group_;
};

} // namespace vortaris
