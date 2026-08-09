#include "world.h"

#include <algorithm>

#include "../serialization/snapshot.h"

namespace vortaris {

void World::serialize_snapshot(BinaryBuffer &r_out) const {
	serialize_world_snapshot(*this, r_out);
}

bool World::deserialize_snapshot(BinaryBuffer &r_in) {
	return deserialize_world_snapshot(*this, r_in);
}

World::World() {
	// The empty archetype hosts every entity with no components yet.
	empty_archetype_ = _create_archetype({});
}

World::~World() {
	for (Archetype *a : archetype_list_) {
		delete a;
	}
}

// ---------------------------------------------------------------- entities --

Entity World::create_entity() {
	Entity e = _alloc_entity_id();
	uint32_t row = empty_archetype_->add_entity(e, change_tick_);
	entity_locations_[e] = { empty_archetype_, row };
	_invalidate_cache();
	return e;
}

Entity World::create_entity_preassigned(uint64_t p_id) {
	uint32_t slot = static_cast<uint32_t>(p_id & 0xFFFFFFFFu) - 1;
	if (slot == 0xFFFFFFFFu) {
		return Entity{};
	}
	// Guard against runaway ids: slot_generations_ is grown to `slot`, so an
	// arbitrarily large id would allocate a huge generation array.
	constexpr uint32_t k_max_slot = (1u << 24); // 16M slots is ample for any world
	if (slot >= k_max_slot) {
		return Entity{};
	}
	if (slot >= slot_generations_.size()) {
		slot_generations_.resize(static_cast<size_t>(slot) + 1, 0);
	}
	Entity e{ p_id };
	if (entity_locations_.count(e) > 0) {
		return Entity{}; // slot already occupied by a live entity
	}
	slot_generations_[slot] = e.generation();
	uint32_t row = empty_archetype_->add_entity(e, change_tick_);
	entity_locations_[e] = { empty_archetype_, row };
	_invalidate_cache();
	return e;
}

void World::destroy_entity(Entity p_e) {
	auto it = entity_locations_.find(p_e);
	if (it == entity_locations_.end()) {
		return;
	}
	Archetype *a = it->second.archetype;
	// Collect component types before removal; dispatch after, so observers that
	// inspect the entity's remaining components see the final (empty) state.
	std::vector<ComponentTypeId> removed_types = a->component_ids;
	uint32_t removed_row = it->second.row;
	Entity moved = a->remove_entity(p_e);
	entity_locations_.erase(it);
	if (moved) {
		auto mit = entity_locations_.find(moved);
		if (mit != entity_locations_.end()) {
			mit->second.row = removed_row;
		}
	}
	_free_entity_id(p_e);
	for (ComponentTypeId t : removed_types) {
		observer_dispatch_.dispatch(ObserverEventType::Removed, p_e, t, godot::String(), godot::Variant());
	}
	_invalidate_cache();
}

void World::destroy_entity_deferred(Entity p_e) {
	if (defer_moves_) {
		if (is_alive(p_e)) {
			pending_destroy_.insert(p_e);
			_queue_deferred_move(p_e);
		}
		return;
	}
	destroy_entity(p_e);
}

bool World::is_alive(Entity p_e) const {
	return p_e && entity_locations_.count(p_e) > 0;
}

void World::set_entity_range(uint32_t p_base_index) {
	if (slot_generations_.size() < p_base_index) {
		slot_generations_.resize(p_base_index, 0);
	}
}

uint32_t World::entity_count() const {
	return static_cast<uint32_t>(entity_locations_.size());
}

// ------------------------------------------------------------ components --

bool World::has(Entity p_e, ComponentTypeId p_t) const {
	if (!is_alive(p_e)) {
		return false;
	}
	auto it = entity_locations_.find(p_e);
	return it->second.archetype->has_component(p_t);
}

void *World::get_raw(Entity p_e, ComponentTypeId p_t) {
	if (!is_alive(p_e)) {
		return nullptr;
	}
	auto it = entity_locations_.find(p_e);
	Archetype *a = it->second.archetype;
	if (!a->has_component(p_t)) {
		return nullptr;
	}
	return a->column(p_t).row(it->second.row);
}

const void *World::get_raw(Entity p_e, ComponentTypeId p_t) const {
	if (!is_alive(p_e)) {
		return nullptr;
	}
	auto it = entity_locations_.find(p_e);
	const Archetype *a = it->second.archetype;
	if (!a->has_component(p_t)) {
		return nullptr;
	}
	return a->column(p_t).row(it->second.row);
}

void World::add_raw(Entity p_e, ComponentTypeId p_t, const void *p_data) {
	if (!is_alive(p_e)) {
		return;
	}
	const ComponentSchema *schema = registry().schema_of(p_t);
	if (!schema) {
		return;
	}

	if (defer_moves_) {
		PendingComponentOp op;
		op.type = p_t;
		op.add = true;
		if (p_data) {
			op.data.assign(static_cast<const uint8_t *>(p_data), static_cast<const uint8_t *>(p_data) + schema->size);
		}
		pending_ops_[p_e].push_back(std::move(op));
		_queue_deferred_move(p_e);
		return;
	}

	auto it = entity_locations_.find(p_e);
	Archetype *cur = it->second.archetype;
	if (cur->has_component(p_t)) {
		uint32_t row = it->second.row;
		Column &col = cur->column(p_t);
		void *dst = col.row(row);
		if (p_data) {
			std::memcpy(dst, p_data, schema->size);
		} else {
			std::memset(dst, 0, schema->size);
		}
		col.mark_changed(row, ++change_tick_);
		observer_dispatch_.dispatch(ObserverEventType::Changed, p_e, p_t, godot::String(), godot::Variant());
		return;
	}

	std::vector<ComponentTypeId> new_ids = cur->component_ids;
	new_ids.push_back(p_t);
	std::sort(new_ids.begin(), new_ids.end());
	Archetype *target = _ensure_archetype(new_ids);
	uint32_t new_row = _move_entity_to(p_e, cur, target, it->second.row);
	entity_locations_[p_e] = { target, new_row };
	Column &col = target->column(p_t);
	void *dst = col.row(new_row);
	if (p_data) {
		std::memcpy(dst, p_data, schema->size);
	} else {
		std::memset(dst, 0, schema->size);
	}
	col.mark_changed(new_row, ++change_tick_);
	observer_dispatch_.dispatch(ObserverEventType::Added, p_e, p_t, godot::String(), godot::Variant());
	_invalidate_cache();
}

void World::remove_component(Entity p_e, ComponentTypeId p_t) {
	if (!is_alive(p_e)) {
		return;
	}
	if (defer_moves_) {
		PendingComponentOp op;
		op.type = p_t;
		op.add = false;
		pending_ops_[p_e].push_back(std::move(op));
		_queue_deferred_move(p_e);
		return;
	}

	auto it = entity_locations_.find(p_e);
	Archetype *cur = it->second.archetype;
	if (!cur->has_component(p_t)) {
		return;
	}
	std::vector<ComponentTypeId> new_ids = cur->component_ids;
	auto it2 = std::find(new_ids.begin(), new_ids.end(), p_t);
	if (it2 != new_ids.end()) {
		new_ids.erase(it2);
	}
	Archetype *target = _ensure_archetype(new_ids);
	uint32_t new_row = _move_entity_to(p_e, cur, target, it->second.row);
	entity_locations_[p_e] = { target, new_row };
	observer_dispatch_.dispatch(ObserverEventType::Removed, p_e, p_t, godot::String(), godot::Variant());
	_invalidate_cache();
}

void World::mark_changed(Entity p_e, ComponentTypeId p_t) {
	if (!is_alive(p_e)) {
		return;
	}
	auto it = entity_locations_.find(p_e);
	Archetype *a = it->second.archetype;
	if (!a->has_component(p_t)) {
		return;
	}
	Column &col = a->column(p_t);
	col.ensure_versions(change_tick_);
	col.mark_changed(it->second.row, ++change_tick_);
	observer_dispatch_.dispatch(ObserverEventType::Changed, p_e, p_t, godot::String(), godot::Variant());
}

void World::get_entity_component_types(Entity p_e, std::vector<ComponentTypeId> &r_out) const {
	r_out.clear();
	if (!is_alive(p_e)) {
		return;
	}
	auto it = entity_locations_.find(p_e);
	if (it == entity_locations_.end()) {
		return;
	}
	const Archetype *a = it->second.archetype;
	r_out.assign(a->component_ids.begin(), a->component_ids.end());
}

void World::emit_event(const godot::String &p_name, Entity p_e, const godot::Variant &p_payload) {
	observer_dispatch_.dispatch(ObserverEventType::Custom, p_e, 0, p_name, p_payload);
}

// ----------------------------------------------------------- command api --

void World::register_custom_command(uint32_t p_id, CustomCommandFn p_fn) {
	custom_commands_[p_id] = p_fn;
}

void World::run_custom_command(uint32_t p_id, const void *p_data, size_t p_size) {
	auto it = custom_commands_.find(p_id);
	if (it != custom_commands_.end()) {
		it->second(p_data, p_size);
	}
}

void World::begin_suppress() {
	++suppress_depth_;
}

void World::end_suppress() {
	if (suppress_depth_ > 0) {
		--suppress_depth_;
		if (suppress_depth_ == 0 && pending_cache_invalidation_) {
			pending_cache_invalidation_ = false;
			++cache_version_;
		}
	}
}

void World::begin_deferred_moves() {
	defer_moves_ = true;
}

void World::end_deferred_moves() {
	if (!defer_moves_) {
		return;
	}
	defer_moves_ = false;

	for (Entity e : deferred_entities_) {
		if (pending_destroy_.count(e) > 0) {
			destroy_entity(e);
		}
	}
	for (Entity e : deferred_entities_) {
		if (pending_destroy_.count(e) > 0) {
			continue;
		}
		_commit_deferred_move(e);
	}

	pending_ops_.clear();
	pending_destroy_.clear();
	deferred_entities_.clear();
	deferred_set_.clear();

	if (suppress_depth_ == 0 && pending_cache_invalidation_) {
		pending_cache_invalidation_ = false;
		++cache_version_;
	}
}

void World::compact() {
	for (size_t i = 0; i < archetype_list_.size();) {
		Archetype *a = archetype_list_[i];
		if (a != empty_archetype_ && a->entities.empty()) {
			archetypes_.erase(a->signature);
			query_cache_.erase_archetype(a);
			archetype_list_.erase(archetype_list_.begin() + static_cast<std::ptrdiff_t>(i));
			delete a;
		} else {
			++i;
		}
	}
}

void World::clear() {
	// Snapshot load replaces the world. Destroy every entity without dispatching
	// Removed events (loading a save is not "death"); keep archetypes for reuse
	// and the id space (preassigned ids overwrite generations on load).
	std::vector<Entity> all;
	all.reserve(entity_locations_.size());
	for (const auto &kv : entity_locations_) {
		all.push_back(kv.first);
	}
	for (Entity e : all) {
		auto it = entity_locations_.find(e);
		if (it == entity_locations_.end()) {
			continue;
		}
		uint32_t removed_row = it->second.row;
		Archetype *a = it->second.archetype;
		Entity moved = a->remove_entity(e);
		entity_locations_.erase(it);
		if (moved) {
			auto mit = entity_locations_.find(moved);
			if (mit != entity_locations_.end()) {
				mit->second.row = removed_row;
			}
		}
		_free_entity_id(e);
	}
	_invalidate_cache();
}

uint32_t World::changed_baseline(uint64_t p_query_signature, bool *r_existed) {
	auto it = changed_baselines_.find(p_query_signature);
	if (it == changed_baselines_.end()) {
		if (r_existed) {
			*r_existed = false;
		}
		return 0;
	}
	if (r_existed) {
		*r_existed = true;
	}
	return it->second;
}

void World::set_changed_baseline(uint64_t p_query_signature, uint32_t p_tick) {
	changed_baselines_[p_query_signature] = p_tick;
}

Archetype *World::move_entity(Entity p_e, Archetype *p_target, uint32_t *r_row) {
	if (!is_alive(p_e)) {
		return nullptr;
	}
	auto it = entity_locations_.find(p_e);
	Archetype *cur = it->second.archetype;
	if (cur == p_target) {
		if (r_row) {
			*r_row = it->second.row;
		}
		return p_target;
	}
	uint32_t row = _move_entity_to(p_e, cur, p_target, it->second.row);
	entity_locations_[p_e] = { p_target, row };
	_invalidate_cache();
	if (r_row) {
		*r_row = row;
	}
	return p_target;
}

// -------------------------------------------------------------- internals --

Entity World::_alloc_entity_id() {
	uint32_t idx;
	if (free_slots_.empty()) {
		idx = static_cast<uint32_t>(slot_generations_.size());
		slot_generations_.push_back(0);
	} else {
		idx = free_slots_.back();
		free_slots_.pop_back();
	}
	return Entity{ (static_cast<uint64_t>(idx) + 1) | (static_cast<uint64_t>(slot_generations_[idx]) << 32) };
}

void World::_free_entity_id(Entity p_e) {
	uint32_t slot = p_e.slot();
	if (slot >= slot_generations_.size()) {
		return;
	}
	if (slot_generations_[slot] != p_e.generation()) {
		return; // stale handle
	}
	slot_generations_[slot] = p_e.generation() + 1;
	free_slots_.push_back(slot);
}

uint64_t World::_compute_signature(const std::vector<ComponentTypeId> &p_ids) const {
	uint64_t h = 14695981039346656037ull;
	for (ComponentTypeId id : p_ids) {
		uint32_t v = id;
		for (int i = 0; i < 4; ++i) {
			h ^= (v & 0xFF);
			h *= 1099511628211ull;
			v >>= 8;
		}
	}
	return h;
}

Archetype *World::_ensure_archetype(const std::vector<ComponentTypeId> &p_ids) {
	uint64_t sig = _compute_signature(p_ids);
	auto it = archetypes_.find(sig);
	if (it != archetypes_.end()) {
		return it->second;
	}
	return _create_archetype(p_ids);
}

Archetype *World::_create_archetype(const std::vector<ComponentTypeId> &p_ids) {
	Archetype *a = new Archetype();
	a->component_ids = p_ids;
	a->signature = _compute_signature(p_ids);
	for (ComponentTypeId t : p_ids) {
		const ComponentSchema *s = registry().schema_of(t);
		Column col;
		col.init(s->size, s->alignment);
		a->columns.push_back(std::move(col));
	}
	archetypes_[a->signature] = a;
	archetype_list_.push_back(a);
	query_cache_.register_new_archetype(a);
	return a;
}

void World::_invalidate_cache() {
	if (suppress_depth_ > 0) {
		pending_cache_invalidation_ = true;
		return;
	}
	++cache_version_;
}

void World::_queue_deferred_move(Entity p_e) {
	if (deferred_set_.insert(p_e).second) {
		deferred_entities_.push_back(p_e);
	}
}

void World::_commit_deferred_move(Entity p_e) {
	auto it = entity_locations_.find(p_e);
	if (it == entity_locations_.end()) {
		return;
	}
	Archetype *cur = it->second.archetype;
	auto pit = pending_ops_.find(p_e);
	if (pit == pending_ops_.end()) {
		return;
	}
	const std::vector<PendingComponentOp> &ops = pit->second;
	std::unordered_set<ComponentTypeId> original_set(cur->component_ids.begin(), cur->component_ids.end());

	// Apply ops in order to compute the desired final component set. The final
	// set is the single source of truth: an add inserts a type, a remove erases
	// it, so [add A, remove A] nets to "no A" and [remove A, add A] to "has A".
	// For every type that survives, keep the data of its LAST add op (later
	// writes win); types never written keep their existing value.
	std::unordered_set<ComponentTypeId> final_set = original_set;
	std::unordered_map<ComponentTypeId, const std::vector<uint8_t> *> last_add_data;
	for (const auto &op : ops) {
		if (op.add) {
			final_set.insert(op.type);
			last_add_data[op.type] = op.data.empty() ? nullptr : &op.data;
		} else {
			final_set.erase(op.type);
		}
	}

	std::vector<ComponentTypeId> final_ids(final_set.begin(), final_set.end());
	std::sort(final_ids.begin(), final_ids.end());

	bool structural = final_ids.size() != cur->component_ids.size() ||
			!std::equal(final_ids.begin(), final_ids.end(), cur->component_ids.begin());

	Archetype *host = cur;
	uint32_t row;
	if (structural) {
		host = _ensure_archetype(final_ids);
		uint32_t new_row = _move_entity_to(p_e, cur, host, it->second.row);
		entity_locations_[p_e] = { host, new_row };
		row = new_row;
	} else {
		row = it->second.row;
	}

	// Apply surviving component writes (value of the last add op for the type).
	for (ComponentTypeId t : final_ids) {
		auto addit = last_add_data.find(t);
		if (addit == last_add_data.end()) {
			continue;
		}
		Column &col = host->column(t);
		void *dst = col.row(row);
		if (addit->second) {
			std::memcpy(dst, addit->second->data(), addit->second->size());
		} else {
			const ComponentSchema *s = registry().schema_of(t);
			std::memset(dst, 0, s->size);
		}
		col.mark_changed(row, ++change_tick_);
	}

	// Dispatch net-change events, one per type against the original set:
	// final \ original -> Added, original \ final -> Removed, present in both
	// and actually written -> Changed.
	for (ComponentTypeId t : final_ids) {
		if (original_set.count(t) == 0) {
			observer_dispatch_.dispatch(ObserverEventType::Added, p_e, t, godot::String(), godot::Variant());
		} else if (last_add_data.count(t) > 0) {
			observer_dispatch_.dispatch(ObserverEventType::Changed, p_e, t, godot::String(), godot::Variant());
		}
	}
	for (ComponentTypeId t : cur->component_ids) {
		if (final_set.count(t) == 0) {
			observer_dispatch_.dispatch(ObserverEventType::Removed, p_e, t, godot::String(), godot::Variant());
		}
	}
	if (structural) {
		_invalidate_cache();
	}
}

uint32_t World::_move_entity_to(Entity p_e, Archetype *p_from, Archetype *p_to, uint32_t p_from_row) {
	uint32_t new_row = p_to->add_entity(p_e, change_tick_);
	for (size_t i = 0; i < p_from->component_ids.size(); ++i) {
		ComponentTypeId tid = p_from->component_ids[i];
		size_t idx = p_to->column_index(tid);
		if (idx != SIZE_MAX) {
			const ComponentSchema *s = registry().schema_of(tid);
			std::memcpy(p_to->columns[idx].row(new_row), p_from->columns[i].row(p_from_row), s->size);
		}
	}
	Entity moved = p_from->remove_entity(p_e);
	if (moved) {
		auto mit = entity_locations_.find(moved);
		if (mit != entity_locations_.end()) {
			mit->second.row = p_from_row;
		}
	}
	return new_row;
}

} // namespace vortaris
