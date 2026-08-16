#include "vecs_scheduler.h"

#include <algorithm>

#include "../core/world.h"
#include "vecs_system.h"

using namespace godot;

namespace vortaris {

void SystemScheduler::add_system(VECSSystem *p_sys) {
	if (!p_sys) {
		return;
	}
	StringName key(p_sys->get_group());
	systems_by_group_[key].push_back(p_sys);
	order_cache_.erase(key);
}

void SystemScheduler::remove_system(VECSSystem *p_sys) {
	if (!p_sys) {
		return;
	}
	StringName key(p_sys->get_group());
	auto it = systems_by_group_.find(key);
	if (it != systems_by_group_.end()) {
		std::vector<VECSSystem *> &vec = it->second;
		vec.erase(std::remove(vec.begin(), vec.end(), p_sys), vec.end());
		if (vec.empty()) {
			systems_by_group_.erase(it);
		}
	}
	order_cache_.erase(key);
	next_run_.erase(p_sys);
}

void SystemScheduler::recompute_order(const StringName &p_group) {
	auto it = systems_by_group_.find(p_group);
	if (it == systems_by_group_.end()) {
		order_cache_.erase(p_group);
		return;
	}
	const std::vector<VECSSystem *> &systems = it->second;

	// Build dependency graph: system name -> index.
	std::unordered_map<godot::StringName, size_t> name_to_idx;
	for (size_t i = 0; i < systems.size(); ++i) {
		name_to_idx[godot::StringName(systems[i]->get_system_name())] = i;
	}

	std::vector<int> indegree(systems.size(), 0);
	std::vector<std::vector<size_t>> out_edges(systems.size());

	for (size_t i = 0; i < systems.size(); ++i) {
		std::vector<SystemDep> deps;
		systems[i]->_deps(deps);
		for (const SystemDep &d : deps) {
			auto tit = name_to_idx.find(godot::StringName(d.target));
			if (tit == name_to_idx.end()) {
				continue; // unknown target: ignore the constraint
			}
			size_t j = tit->second;
			if (d.order == SystemOrder::After) {
				out_edges[j].push_back(i);
				++indegree[i];
			} else if (d.order == SystemOrder::Before) {
				out_edges[i].push_back(j);
				++indegree[j];
			}
		}
	}

	// Stable Kahn's algorithm (insertion order tie-break).
	std::vector<size_t> order;
	std::vector<size_t> queue;
	for (size_t i = 0; i < systems.size(); ++i) {
		if (indegree[i] == 0) {
			queue.push_back(i);
		}
	}
	while (!queue.empty()) {
		size_t i = queue.front();
		queue.erase(queue.begin());
		order.push_back(i);
		for (size_t j : out_edges[i]) {
			if (--indegree[j] == 0) {
				queue.push_back(j);
			}
		}
	}
	// Cycle fallback: append whatever is left in insertion order.
	for (size_t i = 0; i < systems.size(); ++i) {
		if (indegree[i] > 0) {
			order.push_back(i);
		}
	}

	std::vector<VECSSystem *> sorted;
	sorted.reserve(order.size());
	for (size_t i : order) {
		sorted.push_back(systems[i]);
	}
	order_cache_[p_group] = std::move(sorted);
}

std::vector<VECSSystem *> &SystemScheduler::ordered(const StringName &p_group) {
	auto it = order_cache_.find(p_group);
	if (it == order_cache_.end()) {
		recompute_order(p_group);
		it = order_cache_.find(p_group);
	}
	if (it == order_cache_.end()) {
		static std::vector<VECSSystem *> empty;
		return empty;
	}
	return it->second;
}

void SystemScheduler::process(World &p_world, double p_delta, const godot::String &p_group) {
	StringName key(p_group);
	double &elapsed = elapsed_by_group_[key];
	elapsed += p_delta;
	auto it = systems_by_group_.find(key);
	if (it == systems_by_group_.end() || it->second.empty()) {
		return;
	}

	std::vector<VECSSystem *> &systems = ordered(key);
	for (VECSSystem *sys : systems) {
		if (!sys->get_active() || sys->get_paused()) {
			continue;
		}
		if (sys->get_tick_interval() > 0.0) {
			double last = next_run_.count(sys) ? next_run_[sys] : 0.0;
			if (elapsed < last + sys->get_tick_interval()) {
				continue;
			}
			next_run_[sys] = elapsed;
		}
		sys->handle(p_delta);
		if (sys->get_flush_mode() == VECSSystem::PER_SYSTEM) {
			p_world.flush_command_buffers();
		}
	}

	// PER_GROUP: flush once after the whole group ran.
	for (VECSSystem *sys : systems) {
		if (sys->get_flush_mode() == VECSSystem::PER_GROUP) {
			p_world.flush_command_buffers();
			break;
		}
	}
}

void SystemScheduler::clear() {
	systems_by_group_.clear();
	order_cache_.clear();
	next_run_.clear();
	elapsed_by_group_.clear();
}

size_t SystemScheduler::system_count() const {
	size_t n = 0;
	for (const auto &kv : systems_by_group_) {
		n += kv.second.size();
	}
	return n;
}

void SystemScheduler::collect_systems(std::vector<VECSSystem *> &r_out) const {
	r_out.clear();
	for (const auto &kv : systems_by_group_) {
		r_out.insert(r_out.end(), kv.second.begin(), kv.second.end());
	}
}

} // namespace vortaris
