#pragma once

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/world.h"

#include "vecs_scheduler.h"

class VECSEntity;
class VECSComponentType;
class VECSQueryBuilder;
class VECSCommandBuffer;
class VECSSystem;
class VECSObserver;

// A scene/singleton node wrapping one vortaris::World. This is the main
// GDScript-facing entry point; it is registered as the "VECS" engine
// singleton, so scripts use `VECS.create_entity()` etc. directly.
class VECSWorld : public godot::Node {
	GDCLASS(VECSWorld, godot::Node)

public:
	VECSWorld();
	~VECSWorld() override = default;

	vortaris::World &core() { return *core_; }
	const vortaris::World &core() const { return *core_; }

	// ---- entity ----
	godot::Ref<VECSEntity> create_entity();
	godot::Ref<VECSEntity> create_entity_preassigned(int64_t p_id);
	// Pooled entity API. destroy_entity_pooled reclaims the id without bumping
	// its generation, so a stale handle stays valid if the slot is reused by
	// create_entity_pooled (documented trade-off). Pooled slots are never handed
	// out by the regular allocator.
	godot::Ref<VECSEntity> create_entity_pooled();
	void destroy_entity_pooled(const godot::Ref<VECSEntity> &p_entity);
	int64_t pool_size() const;
	// Looks up a live entity by its raw 64-bit id. Returns a null handle when
	// the id is not a live entity in this world (dead, recycled, or <= 0).
	godot::Ref<VECSEntity> entity(int64_t p_id) const;
	// Returns true when an entity with this raw 64-bit id is alive here.
	bool has_entity(int64_t p_id) const;
	void destroy_entity(const godot::Ref<VECSEntity> &p_entity);
	bool is_alive(const godot::Ref<VECSEntity> &p_entity) const;
	int64_t entity_count() const;
	void set_entity_range(int64_t p_base);

	// ---- component types ----
	// Registers a schema-only component from a script: no C++ struct needed.
	// `fields` is an Array of Dictionaries: {"name", "type", "count"?, "sync_priority"?, "networked"?}
	// (type is one of the FieldType names: "F32", "Vector3", "Blob", ...).
	bool register_component(const godot::String &p_name, const godot::Array &p_fields);
	godot::Ref<VECSComponentType> get_component_type(const godot::String &p_name);

	// ---- query / commands ----
	godot::Ref<VECSQueryBuilder> query();
	godot::Ref<VECSCommandBuffer> commands();

	// ---- convenience sugar (thin wrappers over the flexible APIs) ----
	// Spawns one entity whose components are given as a Dictionary:
	//   world.spawn({"Position": {"x": 0.0, "y": 0.0}, "Velocity": {"x": 1.0}})
	godot::Ref<VECSEntity> spawn(const godot::Dictionary &p_components);
	// Iterates every entity having all the given components, calling
	// p_callable(ent) per entity WITHOUT materializing an Array.
	void each(const godot::Array &p_components, const godot::Callable &p_callable);
	// One-call field read/write: world.get_field(e, "Position", "x").
	// Returns `default` (null Variant by default) when the component/field is missing.
	godot::Variant get_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_default = godot::Variant());
	void set_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_value);
	// Convenience: returns the first entity carrying every component in
	// p_components, or a null handle. Equivalent to query().with_all(comps).execute_one().
	godot::Ref<VECSEntity> find_by_components(const godot::Array &p_components);

	// ---- systems ----
	void add_system(VECSSystem *p_system);
	void remove_system(VECSSystem *p_system);
	int64_t system_count() const;

	// ---- observers / events ----
	void add_observer(VECSObserver *p_observer);
	void remove_observer(VECSObserver *p_observer);
	// Broadcasts a custom event; returns the number of observer callbacks that
	// actually received it.
	int64_t emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload);
	// Value-compared field subscription: the callback (entity, new_value) fires
	// only when the field actually changed value since the last delivery.
	// Returns a subscription id usable with off().
	int64_t on_field_changed(const godot::String &p_comp, const godot::String &p_field, const godot::Callable &p_callable);
	void off(int64_t p_subscription_id);
	// Custom event subscription: callback (entity, payload) fires for events
	// with the given name. Returns a subscription id usable with unsubscribe_event().
	int64_t subscribe_event(const godot::String &p_name, const godot::Callable &p_callable);
	void unsubscribe_event(int64_t p_subscription_id);

	// ---- cross-world copy / merge ----
	// Copies one entity (all components) from THIS world into `target_world`.
	// The target keeps the source id when its slot is free, else a fresh id.
	// Returns {source_id: target_id}. Copying into the same world clones the
	// entity (buffered through the command buffer).
	godot::Dictionary copy_entity_to(const godot::Ref<VECSEntity> &p_entity, VECSWorld *p_target);
	// Merges every entity of `source_world` into THIS world, returning the total
	// {source_id: target_id} mapping. source == this clones the whole world.
	godot::Dictionary merge_world(VECSWorld *p_source);

	// ---- debug ----
	godot::Dictionary get_debug_stats() const;

	// ---- per-frame driver ----
	void process(double p_delta, const godot::String &p_group);

	void compact();
	// Tears down transient state so the world can be reused or the extension can
	// shut down cleanly: resets the core world (deferred ops, change baselines,
	// command buffer), clears the observer dispatch and the system scheduler.
	// Entities/archetypes are preserved.
	void shutdown();
	VECSWorld *get_world();

	// ---- snapshot serialization ----
	godot::PackedByteArray serialize_snapshot() const;
	bool deserialize_snapshot(const godot::PackedByteArray &p_data);

	// ---- JSON / data-table helpers (deep Godot integration) ----
	// Registers several schema components at once: { "Name": [ {name,type,...}, ... ], ... }
	bool register_components(const godot::Dictionary &p_components);
	// Batch-creates entities from data: [{ "id"?: int, "components": { "Name": {fields...} } }, ...].
	// Returns the created VECSEntity array (entities that failed to spawn are skipped).
	godot::Array spawn_from_data(const godot::Array &p_entities);
	// Like spawn_from_data, but also returns a mapping Dictionary
	// {source_id_or_index: new_id} so callers can rewrite cross-entity references
	// after the batch (see remap_reference). Entries that carry an "id" are keyed
	// by that id; entries without one are keyed by their array index.
	godot::Dictionary spawn_from_data_mapped(const godot::Array &p_entities);
	// Exports every entity as [{ "id", "components": { "Name": {fields...} } }] (deterministic order).
	godot::Array entities_to_data();
	// World save as a JSON-able Dictionary: { "version", "entities": [...] }.
	godot::Dictionary serialize_snapshot_json();
	// Loads a world save. Accepts either a Dictionary or a JSON String (parsed with Godot's JSON).
	bool deserialize_snapshot_json(const godot::Variant &p_data);
	// Like deserialize_snapshot_json, but also returns the id mapping produced by
	// spawn_from_data_mapped (see there). The save replaces the world first.
	godot::Dictionary deserialize_snapshot_json_mapped(const godot::Variant &p_data);
	// Rewrites a component field that stores a source entity id: reads
	// entity.comp.field as an int, looks it up in `map` (from
	// spawn_from_data_mapped / deserialize_snapshot_json_mapped), and writes the
	// mapped new id back. No-op when the field is missing or not in the map.
	void remap_reference(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Dictionary &p_map);

protected:
	static void _bind_methods();

private:
	// Shared spawn implementation. Fills r_mapping with {source_id_or_index:
	// new_id} and returns the array of spawned VECSEntity handles.
	godot::Array _spawn_from_data_impl(const godot::Array &p_entities, godot::Dictionary &r_mapping);
	void _clear_field_subs();
	void _clear_event_subs();

	struct FieldSubscription {
		vortaris::ObserverId observer_id = 0;
		vortaris::ComponentTypeId comp = 0;
		godot::StringName field;
		godot::Callable callable;
		std::unordered_map<uint64_t, godot::Variant> cached;
	};
	std::unordered_map<int64_t, FieldSubscription> field_subs_;
	int64_t next_field_sub_id_ = 1;
	std::unordered_map<int64_t, vortaris::ObserverId> event_subs_;
	int64_t next_event_sub_id_ = 1;

	std::unique_ptr<vortaris::World> core_;
	std::unique_ptr<vortaris::SystemScheduler> scheduler_;
};
