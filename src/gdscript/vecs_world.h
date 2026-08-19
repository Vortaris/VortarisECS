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
	// Creates an entity (optionally with a preassigned id) and adds every
	// component in `p_components` in one call — a merge of
	// create_entity[_preassigned] + add_component. `def_id` <= 0 auto-assigns the
	// id; a positive `def_id` preassigns it (the CHANT "row -> component dict"
	// spawn helpers collapse to this). Absent component fields are filled with
	// their schema default (zero / empty string / false / zeroed array slots), so
	// callers only supply the fields they care about. Returns a handle to the new
	// entity, or a null handle if creation failed (unregistered component or an id
	// conflict — nothing is left half-spawned).
	godot::Ref<VECSEntity> create_with_components(int64_t p_def_id, const godot::Dictionary &p_components);
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
	// One-call observer factory (0.3.1): creates a VECSObserver, wires
	// `p_callable` as its callback, configures it from `p_opts`, and registers it
	// with this world in one step. The callback signature is
	//   func(event: int, entity: VECSEntity, payload: Variant) -> void
	// (event is a VECSObserver.Event constant). Returns the observer node; the
	// caller owns it — call free() (which auto-unregisters) when done, or keep it
	// for the world's lifetime and let VECSWorld.shutdown() drop the dispatch.
	//
	// p_opts keys (all optional):
	//   "events"          int bitmask of VECSObserver.Event, or an Array of names
	//                     ("added","removed","changed","matched","unmatched",
	//                     "custom"). Default: CHANGED.
	//   "components"      Array of component names the observer watches
	//                     (default: every component).
	//   "fields"          Array of field names for the CHANGED field filter.
	//   "match_components" Array for MATCHED/UNMATCHED membership tracking.
	//   "custom_event_name" String filter for custom events.
	//   "throttle_tick"   int change-tick throttle for CHANGED delivery.
	//   "flush_mode"      int VECSObserver.FlushMode.
	VECSObserver *create_observer(const godot::Callable &p_callable, const godot::Dictionary &p_opts = godot::Dictionary());
	// Thin convenience over create_observer for the CHANT per-frame-poll
	// replacement:
	//   world.on_changed("Combatant", {"fields": ["hp"], "callable": cb})
	// Creates a CHANGED-only observer watching component `p_comp`. p_opts may
	// carry "fields" (Array), "throttle_tick" (int) and "callable" (the callback;
	// when absent no events are delivered). Returns the observer node (caller
	// owns it; free() to unregister). The callback receives
	// (event: int, entity: VECSEntity, payload: Variant).
	VECSObserver *on_changed(const godot::String &p_comp, const godot::Dictionary &p_opts);
	// Broadcasts a custom event; returns the number of observer callbacks that
	// actually received it.
	int64_t emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload);
	// Event-bus subscription sugar (0.4.0): returns an observer that delivers
	// CUSTOM events named p_name to p_callable. Free with remove_observer().
	VECSObserver *on_event(const godot::String &p_name, const godot::Callable &p_callable);
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

	// ---- debug / logging ----
	godot::Dictionary get_debug_stats() const;
	// Enables/disables detailed verbose logging for this process (also writes the
	// `vortarisecs/general/verbose` project setting). Verbose logs are only
	// emitted in debug builds; in release builds this is a no-op.
	void set_verbose(bool p_on);
	// Whether detailed verbose logging is active for this process: debug build
	// AND the `vortarisecs/general/verbose` project setting (with fallback to
	// the legacy `vortarisecs/verbose` path). Reading it also re-syncs the
	// internal logging cache, so the reported state always agrees with what
	// log_verbose() emits. Always false in release builds.
	bool is_verbose() const;

	// ---- remote runtime monitor (EngineDebugger) ----
	// Returns a JSON-able Dictionary describing the world for the editor's remote
	// ECS monitor (see addons/vortarisecs/editor/ecs_debugger_*.gd):
	// { "protocol", "version", "stats", "components": [...], "systems": [...],
	// "entities": [...] }. The entity table is capped at
	// `vortarisecs/general/max_snapshot_entities` (default 500) with a
	// "truncated": true + "entity_total" flag when the cap cuts it, so a huge
	// world does not serialize megabytes per refresh. Sending the whole entity
	// table every frame is costly, so this is only computed on demand when the
	// editor requests a snapshot.
	godot::Dictionary get_snapshot_data();
	// Internal: receives "vecs:*" messages from the editor debugger (game side).
	// Not bound to GDScript. Responds to "req_snapshot" by sending back a
	// "vecs:snapshot" message carrying [get_snapshot_data()], and to
	// "set_field" (runtime field write) with a "vecs:set_field_result" ack.
	// Returns true when the message was handled (EngineDebugger::call_capture
	// requires a bool return, and logging a non-bool return produces a
	// per-message error spam).
	bool _debugger_capture(const godot::String &p_message, const godot::Variant &p_data);

	// ---- runtime debug write (EngineDebugger) ----
	// Validates the entity is alive, the component is attached, the field exists
	// AND the value's Variant type matches the field's expected type, then applies
	// `value` through the same conversion path as set_field. A type mismatch is
	// rejected ({"ok": false, "error": "type mismatch ..."}) instead of being
	// silently coerced to a zero value by Godot's Variant->T conversion. Used by
	// the editor's remote ECS monitor to edit component values live. Returns
	// {"ok": bool, "error": String}.
	godot::Dictionary debug_set_field(int64_t p_entity_id, const godot::String &p_comp,
			const godot::String &p_field, const godot::Variant &p_value);

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
	// Data-driven schema registration (0.4.0): reads a CSV whose header row has
	// a `schema` column (JSON array of field descriptors) plus `name` and/or
	// `id`. Returns the number of NEW components registered, -1 on unreadable
	// file / missing columns. Idempotent: already-registered names are skipped.
	int64_t register_components_from_csv(const godot::String &p_path);
	// Batch-creates entities from data: [{ "id"?: int, "components": { "Name": {fields...} } }, ...].
	// Returns the created VECSEntity array (entities that failed to spawn are skipped).
	godot::Array spawn_from_data(const godot::Array &p_entities);
	// Like spawn_from_data, but also returns a mapping Dictionary
	// {source_id_or_index: new_id} so callers can rewrite cross-entity references
	// after the batch (see remap_reference). Entries that carry an "id" are keyed
	// by that id; entries without one are keyed by their array index.
	godot::Dictionary spawn_from_data_mapped(const godot::Array &p_entities);
	// Exports every entity as [{ "id", "components": { "Name": {fields...} } }]
	// (deterministic order). `max_entities` caps the export (0 = no limit): the
	// editor's remote monitor uses it so a snapshot of a huge world stays bounded,
	// while save-file serialization passes no cap and always exports every entity.
	godot::Array entities_to_data(int64_t p_max_entities = 0);
	// World save as a JSON-able Dictionary: { "version", "entities": [...] }.
	godot::Dictionary serialize_snapshot_json();
	// World save as a JSON String. Honors `vortarisecs/serialization/compact_json`:
	// true => compact (unindented) JSON, false => pretty-printed with tabs.
	godot::String serialize_snapshot_json_string();
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
	// Parses the "vecs:set_field" payload ([entity_id, comp, field, value]),
	// applies it via debug_set_field, and sends a "vecs:set_field_result" ack
	// [ok, entity_id, comp, field, error] back to the editor debugger.
	void _handle_debug_set_field(const godot::Variant &p_data);
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
