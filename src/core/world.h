#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <godot_cpp/core/error_macros.hpp>

#include "archetype.h"
#include "column.h"
#include "command_buffer.h"
#include "component_registry.h"
#include "component_schema.h"
#include "entity.h"
#include "observer_dispatch.h"
#include "query.h"

namespace vortaris {

class BinaryBuffer;

template <class... Comps>
class View;
template <class... Comps>
class ChangeView;

struct EntityLocation {
	Archetype *archetype = nullptr;
	uint32_t row = 0;
};

// One deferred component write queued for an entity during a command-buffer
// flush. Ops are applied in order, so the last write to a field wins.
struct PendingComponentOp {
	ComponentTypeId type = 0;
	bool add = true;
	std::vector<uint8_t> data;
};

// The pure C++ ECS core. No Godot objects, no Variant in hot paths — systems
// written in C++ access components through the typed templates below and only
// the GDScript-facing wrapper (VECSWorld) ever goes through Variant.
//
// Structural changes issued from a System must go through the CommandBuffer
// (commands()) so they are deferred to a safe point; the same CommandBuffer is
// what merges multiple changes to one entity into a single archetype move.
class World {
public:
	World();
	~World();

	World(const World &) = delete;
	World &operator=(const World &) = delete;

	// ---- entity ----
	Entity create_entity();
	Entity create_entity_preassigned(uint64_t p_id); // network / deserialization
	void destroy_entity(Entity p_e);
	void destroy_entity_deferred(Entity p_e);
	bool is_alive(Entity p_e) const;
	void set_entity_range(uint32_t p_base_index);
	uint32_t entity_count() const;

	// ---- components: typed access (C++ systems) ----
	template <class T>
	bool has(Entity p_e) const;
	template <class T>
	T *get(Entity p_e);
	template <class T>
	const T *get(Entity p_e) const;
	template <class T>
	T &add(Entity p_e, const T &p_value = {});
	template <class T>
	T *try_add(Entity p_e, const T &p_value = {}); // nullptr on dead entity / unknown type
	template <class T>
	void remove(Entity p_e);

	// ---- components: reflected access (by type id) ----
	bool has(Entity p_e, ComponentTypeId p_t) const;
	void *get_raw(Entity p_e, ComponentTypeId p_t);
	const void *get_raw(Entity p_e, ComponentTypeId p_t) const;
	void add_raw(Entity p_e, ComponentTypeId p_t, const void *p_data = nullptr);
	void remove_component(Entity p_e, ComponentTypeId p_t);
	void mark_changed(Entity p_e, ComponentTypeId p_t);
	void get_entity_component_types(Entity p_e, std::vector<ComponentTypeId> &r_out) const;

	// ---- observers / events ----
	ObserverDispatch &observer_dispatch() { return observer_dispatch_; }
	void emit_event(const godot::String &p_name, Entity p_e, const godot::Variant &p_payload);

	// ---- snapshot serialization (deterministic) ----
	void serialize_snapshot(BinaryBuffer &r_out) const;
	bool deserialize_snapshot(BinaryBuffer &r_in);

	// ---- iteration ----
	template <class... Comps, class F>
	void for_each(F &&p_fn);
	template <class... Comps, class F>
	void for_each_enabled(F &&p_fn);

	// ---- cached views / change-aware views ----
	template <class... Comps>
	View<Comps...> view(); // compile once, reuse across ticks
	template <class... Comps>
	ChangeView<Comps...> changes(); // yield entities whose Comps changed since last take()

	// ---- change clock ----
	uint32_t change_tick() const { return change_tick_; }
	void advance_change_tick() { ++change_tick_; }

	// ---- command buffer ----
	CommandBuffer &commands() { return command_buffer_; }
	void flush_command_buffers() { command_buffer_.execute(*this); }
	using CustomCommandFn = void (*)(const void *p_data, size_t p_size);
	void register_custom_command(uint32_t p_id, CustomCommandFn p_fn);
	void run_custom_command(uint32_t p_id, const void *p_data, size_t p_size);

	// ---- suppression / deferred moves ----
	void begin_suppress();
	void end_suppress();
	void begin_deferred_moves();
	void end_deferred_moves();
	bool defer_moves_active() const { return defer_moves_; }

	// ---- archetype lifecycle ----
	void compact();
	// Destroys every entity without dispatching observer events. Used when a
	// snapshot/save load replaces the whole world — loading is not "death", so
	// observers (including network sync) are not told about the removed
	// entities. Archetypes and the id space are kept for reuse.
	void clear();

	// ---- iteration guards ----
	// Structural changes (add/remove component, destroy entity) inside a
	// for_each / view iteration would corrupt the archetype rows being walked;
	// the guards below turn that silent corruption into a loud error. Defer
	// structural changes to the command buffer instead.
	void begin_iteration() { ++iteration_depth_; }
	void end_iteration() { --iteration_depth_; }
	bool in_iteration() const { return iteration_depth_ > 0; }

	// ---- caches ----
	uint32_t cache_version() const { return cache_version_; }
	QueryCache &query_cache() { return query_cache_; }
	const std::vector<Archetype *> &all_archetypes() const { return archetype_list_; }
	uint32_t changed_baseline(uint64_t p_query_signature, bool *r_existed);
	void set_changed_baseline(uint64_t p_query_signature, uint32_t p_tick);

	ComponentRegistry &registry() { return ComponentRegistry::instance(); }

	// ---- internals used by CommandBuffer / archetype transitions ----
	Archetype *move_entity(Entity p_e, Archetype *p_target, uint32_t *r_row);

private:
	Entity _alloc_entity_id();
	void _free_entity_id(Entity p_e);
	Archetype *_ensure_archetype(const std::vector<ComponentTypeId> &p_ids);
	Archetype *_create_archetype(const std::vector<ComponentTypeId> &p_ids);
	uint64_t _compute_signature(const std::vector<ComponentTypeId> &p_ids) const;
	void _invalidate_cache();
	void _queue_deferred_move(Entity p_e);
	void _commit_deferred_move(Entity p_e);
	uint32_t _move_entity_to(Entity p_e, Archetype *p_from, Archetype *p_to, uint32_t p_from_row);

	std::vector<uint32_t> slot_generations_;
	std::vector<uint32_t> free_slots_;
	std::unordered_map<Entity, EntityLocation> entity_locations_;
	// Keyed by the exact sorted component set (no hash), so two distinct sets can
	// never collide and share columns. Archetype::signature (a hash) is kept only
	// for stable serialization ordering.
	std::map<std::vector<ComponentTypeId>, Archetype *> archetypes_;
	std::vector<Archetype *> archetype_list_;
	Archetype *empty_archetype_ = nullptr;
	QueryCache query_cache_;
	CommandBuffer command_buffer_;
	uint32_t change_tick_ = 1;
	uint32_t cache_version_ = 0;

	int suppress_depth_ = 0;
	int iteration_depth_ = 0;
	bool pending_cache_invalidation_ = false;
	bool defer_moves_ = false;
	std::vector<Entity> deferred_entities_;
	std::unordered_set<Entity> deferred_set_;
	std::unordered_map<Entity, std::vector<PendingComponentOp>> pending_ops_;
	std::unordered_set<Entity> pending_destroy_;

	std::unordered_map<uint64_t, uint32_t> changed_baselines_;
	std::unordered_map<uint32_t, CustomCommandFn> custom_commands_;
	ObserverDispatch observer_dispatch_;
};

namespace detail {
template <class... Comps, class F, size_t... I>
void for_each_row(F &p_fn, Archetype *p_a, size_t p_row,
		const std::array<ComponentTypeId, sizeof...(Comps)> &p_ids, std::index_sequence<I...>) {
	p_fn(p_a->entities[p_row], *static_cast<Comps *>(p_a->column(p_ids[I]).row(p_row))...);
}
} // namespace detail

template <class T>
bool World::has(Entity p_e) const {
	ComponentTypeId t = type_id_of<T>();
	return t != INVALID_COMPONENT_TYPE && has(p_e, t);
}

template <class T>
T *World::get(Entity p_e) {
	ComponentTypeId t = type_id_of<T>();
	if (t == INVALID_COMPONENT_TYPE) {
		return nullptr;
	}
	return static_cast<T *>(get_raw(p_e, t));
}

template <class T>
const T *World::get(Entity p_e) const {
	ComponentTypeId t = type_id_of<T>();
	if (t == INVALID_COMPONENT_TYPE) {
		return nullptr;
	}
	return static_cast<const T *>(get_raw(p_e, t));
}

template <class T>
T &World::add(Entity p_e, const T &p_value) {
	ComponentTypeId t = type_id_of<T>();
	if (!is_alive(p_e)) {
		// The signature must return a T&, but a dead entity cannot be written;
		// report loudly instead of silently discarding the caller's data.
		static thread_local T dummy{};
		ERR_PRINT("VortarisECS: World::add<T>() on a dead entity; the value was discarded.");
		return dummy;
	}
	add_raw(p_e, t, &p_value);
	return *static_cast<T *>(get_raw(p_e, t));
}

// Non-fatal variant of add<T>(): returns nullptr (instead of a dummy reference)
// when the entity is dead or the component type is unregistered, so callers can
// branch on failure instead of losing data silently.
template <class T>
T *World::try_add(Entity p_e, const T &p_value) {
	ComponentTypeId t = type_id_of<T>();
	if (t == INVALID_COMPONENT_TYPE || !is_alive(p_e)) {
		return nullptr;
	}
	add_raw(p_e, t, &p_value);
	return static_cast<T *>(get_raw(p_e, t));
}

template <class T>
void World::remove(Entity p_e) {
	ComponentTypeId t = type_id_of<T>();
	if (t != INVALID_COMPONENT_TYPE) {
		remove_component(p_e, t);
	}
}

template <class... Comps, class F>
void World::for_each(F &&p_fn) {
	// The type set is fixed at compile time, so the sorted id list is constant:
	// cache it instead of rebuilding and re-sorting on every call.
	static const std::array<ComponentTypeId, sizeof...(Comps)> s_ids = [] {
		std::array<ComponentTypeId, sizeof...(Comps)> arr = { type_id_of<Comps>()... };
		std::sort(arr.begin(), arr.end());
		return arr;
	}();
	Query q;
	q.all.assign(s_ids.begin(), s_ids.end());
	const auto &arches = query_cache_.match(q, archetype_list_);
	begin_iteration();
	for (Archetype *a : arches) {
		// Rows are always live: remove_entity swap-removes eagerly, so there are
		// no stale rows to skip. Structural changes mid-iteration are rejected
		// by the iteration guard (use the command buffer to defer them).
		for (size_t row = 0; row < a->entities.size(); ++row) {
			detail::for_each_row<Comps...>(p_fn, a, row, s_ids,
					std::make_index_sequence<sizeof...(Comps)>{});
		}
	}
	end_iteration();
}

template <class... Comps, class F>
void World::for_each_enabled(F &&p_fn) {
	static const std::array<ComponentTypeId, sizeof...(Comps)> s_ids = [] {
		std::array<ComponentTypeId, sizeof...(Comps)> arr = { type_id_of<Comps>()... };
		std::sort(arr.begin(), arr.end());
		return arr;
	}();
	Query q;
	q.all.assign(s_ids.begin(), s_ids.end());
	const auto &arches = query_cache_.match(q, archetype_list_);
	begin_iteration();
	for (Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			if (!a->get_enabled(row)) {
				continue;
			}
			detail::for_each_row<Comps...>(p_fn, a, row, s_ids,
					std::make_index_sequence<sizeof...(Comps)>{});
		}
	}
	end_iteration();
}

// Cached, reusable view over every entity matching Comps... The query is
// compiled once in the constructor; each()/count() then run through the
// incrementally-maintained QueryCache, so repeated ticks cost no query
// construction and no per-entity scan. Typed references, zero Variant.
//
//   auto view = world.view<Position, Velocity>();
//   view.each([](Entity e, Position &pos, Velocity &vel) { ... });
template <class... Comps>
class View {
public:
	// Default-constructed views are empty; assign from World::view<T>() in
	// a system's _setup().
	View() = default;

	View(World &p_world) :
			world_(&p_world) {
		ids_ = { type_id_of<Comps>()... };
		query_.all.assign(ids_.begin(), ids_.end());
		std::sort(query_.all.begin(), query_.all.end());
	}

	template <class F>
	void each(F &&p_fn) {
		if (!world_) {
			return;
		}
		const auto &arches = world_->query_cache().match(query_, world_->all_archetypes());
		world_->begin_iteration();
		for (Archetype *a : arches) {
			for (size_t row = 0; row < a->entities.size(); ++row) {
				detail::for_each_row<Comps...>(p_fn, a, row, ids_,
						std::make_index_sequence<sizeof...(Comps)>{});
			}
		}
		world_->end_iteration();
	}

	size_t count() const {
		if (!world_) {
			return 0;
		}
		Query q = query_;
		const auto &arches = world_->query_cache().match(q, world_->all_archetypes());
		size_t n = 0;
		for (const Archetype *a : arches) {
			n += a->entities.size();
		}
		return n;
	}

private:
	World *world_ = nullptr;
	std::array<ComponentTypeId, sizeof...(Comps)> ids_;
	Query query_;
};

// Change-aware view. It pins a baseline change tick; take() returns every
// entity whose watched component column was written since the previous take(),
// then advances the baseline. Ideal for sparse / event-driven systems: instead
// of re-processing the whole matching set every tick, handle only the rows that
// actually changed since the last pass.
//
//   auto changed = world.changes<GravityBlock>();
//   for (Entity e : changed.take()) { ... }
template <class... Comps>
class ChangeView {
public:
	ChangeView() = default;

	ChangeView(World &p_world) :
			world_(&p_world),
			baseline_(p_world.change_tick()) {
		ids_ = { type_id_of<Comps>()... };
		query_.all.assign(ids_.begin(), ids_.end());
		std::sort(query_.all.begin(), query_.all.end());
	}

	std::vector<Entity> take() {
		std::vector<Entity> out;
		if (!world_) {
			return out;
		}
		const auto &arches = world_->query_cache().match(query_, world_->all_archetypes());
		for (Archetype *a : arches) {
			for (ComponentTypeId t : ids_) {
				if (a->has_component(t)) {
					a->column(t).ensure_versions(world_->change_tick());
				}
			}
			for (size_t row = 0; row < a->entities.size(); ++row) {
				bool changed = false;
				for (ComponentTypeId t : ids_) {
					if (a->has_component(t) && a->column(t).row_changed_since(row, baseline_)) {
						changed = true;
						break;
					}
				}
				if (changed) {
					out.push_back(a->entities[row]);
				}
			}
		}
		baseline_ = world_->change_tick();
		return out;
	}

private:
	World *world_;
	uint32_t baseline_;
	std::array<ComponentTypeId, sizeof...(Comps)> ids_;
	Query query_;
};

template <class... Comps>
View<Comps...> World::view() {
	return View<Comps...>(*this);
}

template <class... Comps>
ChangeView<Comps...> World::changes() {
	return ChangeView<Comps...>(*this);
}

} // namespace vortaris
