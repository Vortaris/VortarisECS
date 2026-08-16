# VortarisECS

**English** | [简体中文](README.zh-CN.md)

A modern, data-oriented ECS (Entity-Component-System) framework for **Godot 4.7** written in C++ as a GDExtension (godot-cpp). It is easy to use, robust, extensible and network-synchronizable.

Design reference: [GECS](https://github.com/BlockBreaker-Studios/GECS) — its archetype SoA storage, layered entity ids, deferred command buffer, query builder, observers and hierarchical network sync patterns were ported to native C++.

> **API 权威参考 (Authoritative reference)**: the class reference in
> [`doc_classes/*.xml`](doc_classes/) is the source of truth for the GDScript
> API (it is compiled into the editor's F1 help). If anything in this README
> conflicts with `doc_classes/`, trust `doc_classes/`.

## Features

- **Pure C++ data components** — components are plain trivially-copyable structs stored in cache-friendly archetype columns (SoA). A declarative schema macro exposes fields for GDScript access and binary serialization.
- **Lightweight entity handles** — 64-bit layered ids (slot + generation); O(1) stale-handle detection; network preassigned ids.
- **Fast queries** — archetype membership in O(1); incrementally maintained query cache; `.changed()` change detection.
- **Deferred command buffer** — batched structural changes with a single archetype transition per entity and one cache invalidation per flush.
- **C++-first systems** — typed `world.for_each<Position, Velocity>` hot path with zero Variant overhead; group scheduling with dependency ordering, per-system timers and flush modes; GDScript systems via `_script_process`.
- **Observers / events** — ADDED / REMOVED / CHANGED / MATCHED / UNMATCHED / custom events, re-entrancy safe.
- **Deterministic binary serialization** — little-endian, fixed-width, byte-identical snapshots.
- **Pluggable network sync** — `VECSSyncStrategy` abstraction with a default server-authoritative snapshot replication (dirty-checked deltas + periodic reconciliation + anti-ghost). Transport is Godot's MultiplayerAPI (RPC), with a direct in-process test transport.

## What's new in 0.3.1

Quality-of-life API built from real usage in the CHANT project (162 hand-written
typed casts, ~15 "scan the whole table and compare owner by hand" queries, two
hand-rolled spawns, per-frame UI polling):

- **Typed field access** — `VECSComponent.get_int / get_float / get_bool /
  get_string / get_vector` and `VECSEntity.getf_int / getf_float / getf_bool /
  getf_string / getf_vector` read a field directly as the requested type, so
  `int(comp.get_field("hp"))` becomes `comp.get_int("hp")`.
- **Query field-equality filter** — `query().with_all(["Combatant"])
  .field_equals("Combatant", "owner", eid).execute()` runs the comparison in C++
  (no per-entity GDScript callback), replacing the full-table + hand-compare
  scans. Chainable (AND); applied by `execute` / `execute_one` / `count`.
- **Entity spawn convenience** — `world.create_with_components(def_id, {
  "Comp": {fields...} })` merges create + add_component and fills absent fields
  with their schema default (0 / empty string / false / zeroed array slots).
- **GDScript-usable observer** — `VECSObserver` is now a concrete class
  (`VECSObserver.new()` works directly, root cause: it was registered as a
  virtual class), plus `set_callback(callable)`,
  `world.create_observer(callable, opts)` and the CHANT-targeted
  `world.on_changed("Combatant", {"fields": ["hp"], "callable": cb})` — a drop-in
  replacement for per-frame polling.
- **Array membership** — `VECSComponent.field_contains(name, value)` checks a
  scalar or any array element in one call (the "rebuild the array and scan for a
  0-sentinel" idiom collapses).

## What's new in 0.3.0

- **Hierarchical project settings** — settings reorganized from the flat
  `vortarisecs/verbose` into `vortarisecs/<category>/<name>`, so they group under
  Project Settings > VortarisECS:
  `vortarisecs/general/verbose` (migrated from the 0.3.0 flat path, still
  honored as a fallback), `vortarisecs/general/auto_shutdown_on_exit`,
  `vortarisecs/general/max_snapshot_entities`,
  `vortarisecs/debug/auto_refresh_interval`,
  `vortarisecs/network/default_sync_priority`,
  `vortarisecs/observer/default_throttle_tick` and
  `vortarisecs/serialization/compact_json`. Defaults are only seeded when
  absent, so a user's value is never clobbered.
- **Previously hard-coded defaults became settings** — the sync tier for new
  component fields, the observer CHANGED throttle, the editor remote-monitor
  entity cap / auto-refresh interval, the exit-time `shutdown()` cleanup, and
  whether snapshot JSON strings are compact.
- **`VECSWorld.serialize_snapshot_json_string()`** — JSON save as a String,
  honoring `vortarisecs/serialization/compact_json` (compact vs pretty).
- **`VECSWorld.is_verbose()`** now reports the persisted setting (with legacy
  fallback) rather than an init-time snapshot.

## What's new in 0.3.0 (runtime remote monitoring)

- **Runtime remote monitoring GUI** — while a game runs, the editor's debugger
  bottom panel gets an **"ECS"** tab that shows the *running* game's live world:
  Entities (id → component → field), Components (registry with field metadata),
  Systems (name/group/active) and Stats. Works like Godot's scene-tree Remote
  mode: the editor sends `vecs:req_snapshot` over `EngineDebugger`, the game
  replies with `vecs:snapshot` from the new `VECSWorld.get_snapshot_data()`.
  Refresh button + optional ~1 Hz auto-refresh; snapshots are on-demand only.
- **`VECSWorld.get_snapshot_data()`** — JSON-able Dictionary of the world
  (`stats` / `components` / `systems` / `entities`); used by the editor tab. The
  entity table is capped at `vortarisecs/general/max_snapshot_entities` (default
  500) with a `truncated`/`entity_total` flag, so a 100k-entity world does not
  serialize megabytes per refresh; save files via `serialize_snapshot_json()` are
  never truncated.
- The existing inspector dock (`editor/ecs_inspector_dock.gd`) remains for the
  editor-side world; the new debugger tab is the one that sees the running game.

## What's new in 0.2.1

- **Runtime debug overlay** — the editor inspector dock can only see the
  *editor* process's world (empty), so an in-game overlay
  (`addons/vortarisecs/ecs_overlay.gd` + `ecs_overlay.tscn`) now shows live
  stats, an entity/component/field browser and JSON snapshot export/import for
  the **running** world. Off by default; enable with `--vortaris-ecs-overlay on`
  or toggle with **F2** in-game.
- **Headless CLI** — `--vortaris-ecs-stats`, `--vortaris-ecs-snapshot <path>`
  and `--vortaris-ecs-overlay on|off` (parsed after the world is built, so they
  report the real game world). Output is `[vortarisecs]`-prefixed for AI/scripts.
- **Tiered logging** — new `vortarisecs/verbose` project setting; debug-only run
  logs plus verbose traces (entity birth/death, component writes, observer
  dispatch, network packet detail, query execution), compiled out in release.
  New API: `VECSWorld.set_verbose(bool)` / `VECSWorld.is_verbose()`.
- **AI debugging docs** — new [`docs/AI_DEBUGGING.md`](docs/AI_DEBUGGING.md):
  MCP `run_script` examples, the CLI parameter table, and the editor-dock
  isolation warning. See [`RELEASE_NOTES.md`](RELEASE_NOTES.md) for the full
  changelog.

## What's new in 0.2.0

- **Entity lookup** — `world.entity(id)` / `world.has_entity(id)`; `get_component` now returns a null handle when the component is not attached.
- **Field defaults** — `get_field` / `getf` accept a `default` value returned for missing components/fields.
- **Query ergonomics** — `find_by_components(comps)`, `where(predicate)`, `order_by(comp, field)` and `order_by_id()`; default order is archetype creation order + row order.
- **Array fields** — `VECSComponent.get_field_count` / `get_array_element` / `set_array_element`, plus `VECSComponentType.get_field_count` / `get_field_type` (`"Array:<type>"`).
- **Id mapping** — `spawn_from_data_mapped` / `deserialize_snapshot_json_mapped` return `{source_id_or_index: new_id}`, and `remap_reference` rewrites cross-entity references; `spawn_from_data` entries accept `"parent"` for auto-parenting.
- **Entity pooling** — `create_entity_pooled` / `destroy_entity_pooled` / `pool_size` recycle ids without bumping the generation (stale handles stay valid; documented trade-off).
- **Cross-world copy/merge** — `copy_entity_to` / `merge_world` return id mappings; merging a world into itself clones it.
- **Event bus** — `emit_event` returns the receiver count; `subscribe_event` / `unsubscribe_event`; value-compared `on_field_changed(comp, field, callable)` + `off`.
- **Observer filters** — field-level CHANGED subscription (`set_fields`) and change-tick throttling (`set_throttle_tick`).
- **Networking hardening** — packets are validated before any write (truncated / unknown-schema / id-conflict packets are dropped with no partial state); `sync_priority` now throttles delta sends (REALTIME / HIGH 20 Hz / MEDIUM 10 Hz / LOW 2 Hz).
- **ChangeView optimization** — `take()` skips unchanged archetypes via a per-column max-version fast path and collects from a world write-log incrementally (same results, much faster).
- **StringFixed** — writes are truncated to the fixed capacity on a UTF-8 code-point boundary (whole characters only) and emit a warning; `count == 0` stores an empty string.
- **Robustness** — `shutdown()` resets transient state (deferred ops, change baselines, observer dispatch, scheduler) so a world can be reused; change clock widened to 64-bit; entity-generation wrap-around guard; clean shutdown that removes all exit-time warnings/leaks.
- **Tooling** — `get_debug_stats()`, `VECSQueryBuilder.get_last_execution_time_usec()`, and an editor inspector dock.

## Architecture

```
GDScript layer (VECS-prefixed classes)          C++ core (namespace vortaris)
─────────────────────────────────────           ─────────────────────────────
VECSWorld (Node / "VECS" singleton)  ──────────► World
VECSEntity (RefCounted handle)       ──────────► Entity (64-bit layered id)
VECSComponent (field accessor)       ──────────► Archetype (SoA columns)
VECSComponentType (schema metadata)  ──────────► ComponentRegistry / ComponentSchema
VECSQueryBuilder (fluent)            ──────────► Query / QueryCache
VECSCommandBuffer                    ──────────► CommandBuffer (deferred ops)
VECSSystem (Node, virtuals)          ──────────► SystemScheduler (groups/topo)
VECSObserver (Node, event hooks)     ──────────► ObserverDispatch
VECSWorld snapshot methods           ──────────► BinaryBuffer / snapshot (internal)
VECSNetworkSync (Node, RPC)          ──────────► VECSSyncStrategy (pluggable)
                                                 └─ VECSSnapshotReplication (default)
```

> Note: binary/JSON snapshot serialization lives on `VECSWorld`
> (`serialize_snapshot()` / `serialize_snapshot_json()` and their
> `deserialize_*` counterparts) and maps to the internal
> `vortaris::BinaryBuffer` / snapshot codec — there is **no** `VECSBinaryBuffer`
> class.

## Core API overview

Everything you touch from GDScript is a `VECS`-prefixed class; the C++ core
(`vortaris::`) stays hidden. This is the mental map:

| Class | Role | Most-used entry points |
|---|---|---|
| `VECSWorld` | The world: owns every entity, the component registry, the query cache and the system scheduler | `VECS.get_world()`, `create_entity`, `create_with_components`, `spawn`, `register_component`, `query`, `each`, `commands`, `process`, `serialize_*` / `deserialize_*` |
| `VECSEntity` | Lightweight handle to one entity (slot + generation packed into a 64-bit id) | `add_component`, `get_component`, `has_component`, `getf` / `setf`, `getf_int` / `getf_float` / `getf_bool` / `getf_string` / `getf_vector`, `is_alive`, `get_id` |
| `VECSComponent` | Field-level accessor for one component instance on an entity | `get_field`, `set_field`, `get_int` / `get_float` / `get_bool` / `get_string` / `get_vector`, `get_fields`, `field_contains` |
| `VECSQueryBuilder` | Fluent, chainable query | `with_all` / `with_any` / `with_none`, `changed`, `enabled`, `field_equals`, `where`, `order_by`, `order_by_id`, terminated by `execute` / `execute_one` / `count` |
| `VECSCommandBuffer` | Deferred structural changes (allowed inside iteration) | `add_component`, `remove_component`, `remove_entity`, `flush` |
| `VECSSystem` | Base class for game logic | GDScript: override `_script_process`; C++: override `_setup` / `_tick` / `_deps` |
| `VECSObserver` | Event-driven reactions to component events | `set_callback`, `on_added` / `on_removed` / `on_changed` / `on_matched` / `on_unmatched` / `on_custom`, `set_components`, `set_fields`, `set_throttle_tick` |
| `VECSComponentType` | Immutable schema metadata for a registered component type | `get_field_names`, `get_field_type`, `get_field_count`, `get_size`, `get_id` |
| `VECSNetworkSync` | Network coordinator: binds a world to a sync strategy | `bind_world`, `set_server`, `set_strategy`, `tick`, `set_direct_peer` |
| `VECSSnapshotReplication` | Default server-authoritative replication strategy | `reconciliation_interval` |

Two idioms show up everywhere:

- **Convenience sugar vs. the flexible layer.** `spawn`, `each`, `getf`/`setf`,
  `find_by_components` and the world-level `get_field`/`set_field` are thin
  wrappers over the flexible APIs. Use the sugar for simple things; drop to the
  flexible layer (`query()`, `commands()`, `get_component` + typed getters, C++
  `for_each`) when you need explicit control.
- **Typed access kills the casts.** `int(comp.get_field("hp"))` becomes
  `comp.get_int("hp")`; `float(e.getf("Health", "amount"))` becomes
  `e.getf_float("Health", "amount")`. The typed getters exist on both
  `VECSComponent` (`get_int` / `get_float` / `get_bool` / `get_string` /
  `get_vector`) and `VECSEntity` (`getf_int` / `getf_float` / `getf_bool` /
  `getf_string` / `getf_vector`).

## Build (Windows / MSVC)

The plugin links against **godot-cpp** (external dependency). Get it first (must target Godot 4.7):

```bash
git clone -b 4.7 https://github.com/godotengine/godot-cpp.git godot-cpp
pip install scons
```

Then build the static library and the plugin. Point the plugin build at your
checkout with `godot_cpp_path=<path-to-godot-cpp>` (or the `GODOT_CPP_PATH`
env var; SConstruct also probes common sibling locations):

```bash
cd godot-cpp
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64   # for release exports
cd <this repo>
scons platform=windows target=template_debug arch=x86_64 build_library=False godot_cpp_path=<path-to-godot-cpp>
```

Output lands in `demo/addons/vortarisecs/bin/vortarisecs.windows.*.dll`. The first time you open `demo/` in Godot, the editor generates `.godot/extension_list.cfg` (this registers the extension).

To use VortarisECS in your own project, copy the `addons/vortarisecs/` folder
into your project root and reopen the editor — Godot discovers and loads the
`.gdextension` automatically (it also shows up under Project ▸ Plugins).

Cross-platform: the core is portable and builds on Linux / macOS too — see
[`docs/cross_platform.md`](docs/cross_platform.md). Releases ship a prebuilt
Windows x86_64 plugin; other platforms build once on their own machine.

Run the demo (functional + performance):

```
godot --headless --path demo
godot --headless --path demo --script res://scripts/perf_test.gd
```

## Quick start (GDScript)

A short end-to-end walkthrough lives in [`docs/quickstart.md`](docs/quickstart.md)
(and runs as `demo/scripts/quickstart.gd`). For AI agents debugging a running
game, see [`docs/AI_DEBUGGING.md`](docs/AI_DEBUGGING.md) (MCP `run_script`
examples, headless CLI arguments, runtime overlay). The **convenience API** gets
simple things done in a few lines:

```gdscript
var world: VECSWorld = VECS.get_world()

world.register_component("Pos", [{"name": "x", "type": "F32"}, {"name": "y", "type": "F32"}])
world.register_component("Vel", [{"name": "x", "type": "F32"}])

var e := world.spawn({"Pos": {"x": 1.0, "y": 2.0}, "Vel": {"x": 0.5}})   # one-call spawn

world.each(["Pos", "Vel"], func(ent: VECSEntity) -> void:                 # iterate, no Array built
    ent.setf("Pos", "x", ent.getf("Pos", "x") + ent.getf("Vel", "x")))
```

The full API offers the same things with explicit control — component
accessors, the fluent query builder, command buffers, and typed C++ iteration:

```gdscript
var world: VECSWorld = VECS.get_world()

var e: VECSEntity = world.create_entity()
e.add_component("Position", {"x": 1.0, "y": 2.0, "z": 0.0})
e.add_component("Velocity", {"x": 0.5, "y": 0.0, "z": 0.0})

var pos: VECSComponent = e.get_component("Position")
pos.set_field("x", 3.0)                       # marks the row changed

var hits: Array = world.query() \
    .with_all(["Position", "Velocity"]) \
    .enabled() \
    .execute()
```

### One-liners for the common 0.3.1 patterns

```gdscript
# Typed field access — no int()/float()/bool()/str() casts.
var hp: float = e.getf_float("Health", "amount")     # e: VECSEntity
var lvl: int   = e.get_component("Combatant").get_int("level")

# "Find everything owned by me" — one filtered query, comparison in C++.
var owned: Array = world.query() \
    .with_all(["Combatant"]) \
    .field_equals("Combatant", "owner", eid) \
    .execute()

# Spawn + add components, filling absent fields with schema defaults.
var unit: VECSEntity = world.create_with_components(0, {"Combatant": {"hp": 30.0}})

# Event-driven instead of per-frame polling.
var obs: VECSObserver = world.on_changed("Combatant", {
    "fields": ["hp"],
    "callable": func(_ev: int, ent: VECSEntity, _p: Variant) -> void: print(ent.get_id()),
})

# Array membership in one call.
if e.get_component("Tags").field_contains("tags", "burning"): ...
```

## Script-defined components & systems (no C++)

Components can be defined entirely from a script — no C++ struct needed. The
framework computes the memory layout from the field declarations and stores
them in the same SoA columns; field access, deterministic serialization and
network sync all go through the same schema-reflection pipeline, so script
components behave exactly like C++ ones.

```gdscript
# 1) Register a schema component (types: Bool/I8..I64/U8..U64/F32/F64/
#    Vector2..4(i)/Color/Quaternion/Basis/Transform2D/3D/AABB/Rect2/Plane/
#    StringFixed/Blob; optional keys: count, sync_priority, networked)
world.register_component("Health", [
    {"name": "amount", "type": "F32"},
    {"name": "max", "type": "F32", "sync_priority": 0},
])

# 2) Use it exactly like a C++ component
var e: VECSEntity = world.create_entity()
e.add_component("Health", {"amount": 100.0, "max": 100.0})
var h: VECSComponent = e.get_component("Health")
h.set_field("amount", 75.0)

# 3) A system written in GDScript: extend VECSSystem, override _script_process,
#    and reach the world via get_world_node().
var sys = preload("res://scripts/script_system.gd").new()
sys.group = "scripts"
world.add_system(sys)
world.process(0.1, "scripts")
```

Both authoring styles coexist: script systems are great for game logic and
iteration, C++ systems (next section) for performance-critical hot paths.

## C++ systems (high-performance path)

Components and systems are defined in C++ (compiled into the same dll for this project):

```cpp
// components.h
struct Position { float x = 0, y = 0, z = 0; };
VECS_REGISTER_COMPONENT(Position,
    VECS_FIELD(Position, x, F32),
    VECS_FIELD(Position, y, F32),
    VECS_FIELD(Position, z, F32));

// systems.h
class MoveSystem : public VECSSystem {
    GDCLASS(MoveSystem, VECSSystem)
public:
    void _tick(vortaris::World &w, double delta) override {
        const float dt = float(delta);
        w.for_each<Position, Velocity>([&](vortaris::Entity e, Position &pos, Velocity &vel) {
            pos.x += vel.x * dt;
            pos.y += vel.y * dt;
            pos.z += vel.z * dt;
        });
    }
};
```

## Cached views & change-aware iteration

`for_each` rebuilds its query each call (cheap — the QueryCache is incremental),
but a system that wants an explicit data contract can hold a `View`: compile the
query once in `_setup`, then reuse it every tick. `ChangeView` goes further: it
pins a baseline and `take()` returns only the entities whose watched components
were **written since the previous take** — ideal for sparse, event-driven
systems (sand falling, AI triggers) that shouldn't re-scan the whole set.

```cpp
class GravitySystem : public VECSSystem {
    GDCLASS(GravitySystem, VECSSystem)
public:
    void _setup(vortaris::World &w) override {
        view_    = w.view<Position, Velocity>();  // compile once
        changes_ = w.changes<GravityBlock>();     // change-aware
    }
    void _tick(vortaris::World &w, double dt) override {
        view_.each([](vortaris::Entity e, Position &p, Velocity &v) { /* ... */ });
        for (vortaris::Entity e : changes_.take()) { /* only changed rows */ }
    }
private:
    vortaris::View<Position, Velocity> view_;
    vortaris::ChangeView<GravityBlock> changes_;
};
```

The script equivalent is an **active set**: an event/observer adds a marker
component (e.g. `Falling`), and the system only queries entities carrying it.
See `demo/scripts/falling_system.gd` + `sand_observer.gd` for the full sand
falling example.

## Iteration contract

Never issue structural changes (add/remove a component, destroy an entity)
**while iterating** — `for_each`, `View::each` and `VECSWorld::each` walk live
archetype rows, and a structural change would move rows under the walker. Defer
them to the command buffer (`world.commands()`) or do them outside the loop:

```cpp
w.for_each<Position>([&](vortaris::Entity e, Position &pos) {
    // reading/writing component VALUES is fine ...
});
// ... but adding/removing components or destroying entities is not, inside the
// loop. Defer: w.commands().add_component(...) then flush once.
```

The framework enforces this: a structural change issued during iteration is
rejected with an error instead of silently corrupting the iteration (it used to
cause random skips / stale reads).

## Best practices & common pitfalls

- **Never issue structural changes inside iteration.** `for_each`, `View::each`
  and `world.each()` walk live archetype rows; adding/removing a component or
  destroying an entity mid-loop moves rows under the walker. Defer them to
  `world.commands()` and flush once (the framework **rejects** in-loop
  structural changes with a loud error). Reading/writing component *values* in a
  loop is always fine.
- **"Owner foreign key" lookups → `field_equals`.** Instead of
  `query().with_all(["Combatant"]).execute()` then scanning every result to
  hand-compare the `owner` field, filter in C++:
  `query().with_all(["Combatant"]).field_equals("Combatant", "owner", eid).execute()`.
- **Replace per-frame polling with observers.** A UI that reads `hp` every frame
  can subscribe instead: `world.on_changed("Combatant", {"fields": ["hp"],
  "callable": cb})` fires only when `hp` actually changed. A `set_throttle_tick`
  caps CHANGED delivery deterministically for high-frequency writes.
- **Sparse spawns → `create_with_components`.** `world.create_with_components(
  def_id, {"Health": {"amount": 75}})` fills absent fields with their schema
  default, so you only write the fields you care about. `def_id <= 0`
  auto-assigns the id; a positive `def_id` preassigns it.
- **Null handles are the "not found" answer.** `VECSEntity.get_component`,
  `VECSWorld.entity(id)`, `VECSWorld.get_component_type` return a **null handle**
  (never an invalid wrapper) when the thing is missing — test with `== null`.
- **Default values on reads.** `get_field` / `getf` / `VECSComponent.get_field`
  take a `default` Variant (null by default) returned when the component/field is
  missing. Use a real value, not a magic sentinel.
- **Array membership → `field_contains`.** `comp.field_contains("tags", value)`
  checks a scalar or any array element in one call — no manual rebuild + scan.
- **Large worlds.** Prefer `world.each(comps, callable)` or C++ `for_each` over
  `query().execute()` (which materializes an Array). Use the binary
  `serialize_snapshot()` for big saves; JSON is convenient but slower. The
  editor's remote monitor caps its entity table at
  `vortarisecs/general/max_snapshot_entities`, so a 100k-entity world stays
  bounded.
- **Pooled ids stay valid.** `create_entity_pooled` / `destroy_entity_pooled`
  recycle a slot **without** bumping the generation, so a stale handle captured
  before a pooled destroy stays valid if the slot is reused. Documented
  trade-off — don't combine pooling with generation-based assumptions.
- **Snapshot load is not "death".** Loading a save or applying a network
  full-state replaces the world and does **not** dispatch REMOVED observers.
- **Change tracking is lazy.** A column starts tracking versions on first
  `changed()` use and stamps pre-existing rows, so the first pass reports them
  once. `.changed()` baselines are tracked per query, so filters don't interfere.

## JSON saves & data tables

Deeply integrated with Godot's own `JSON` class — pass in/out plain
`Dictionary` / `Array` / `String`, stringify/parse with the engine:

```gdscript
# World save (archive): serialize → stringify → write file
var text: String = world.serialize_snapshot_json_string()  # honors vortarisecs/serialization/compact_json
# ... or stringify manually: JSON.stringify(world.serialize_snapshot_json(), "\t")
FileAccess.open("user://save.json", FileAccess.WRITE).store_string(text)

# Load: read file → feed the JSON string straight back in
var ok: bool = world.deserialize_snapshot_json(FileAccess.get_file_as_string("user://save.json"))
```

```gdscript
# Data tables (e.g. cards): bulk-register component schemas, batch-spawn
world.register_components({
    "Card":   [{"name": "title", "type": "StringFixed", "count": 64}],
    "Effect": [{"name": "damage", "type": "F32"}, {"name": "kind", "type": "I32"}],
    "Cost":   [{"name": "mana", "type": "I32"}],
})
var deck: Array = world.spawn_from_data([
    {"components": {"Card": {"title": "Fireball"}, "Effect": {"damage": 15.0}, "Cost": {"mana": 3}}},
    # ... typically produced by a CSV→JSON pipeline
])
var exported: Array = world.entities_to_data()   # [{ "id", "components": {...} }, ...]
```

`deserialize_snapshot_json` accepts either a `Dictionary` (already parsed) or
a JSON `String`; unknown components are skipped with a warning, entity ids are
preserved (preassigned) and the save carries a version number. Script
(schema-only) components serialize identically to C++ ones.

## Network sync

```gdscript
var server_ns: VECSNetworkSync = VECSNetworkSync.new()
server_ns.set_server(true)
server_ns.bind_world(world)              # server world

var client_ns: VECSNetworkSync = VECSNetworkSync.new()
client_ns.bind_world(client_world)       # client world
server_ns.set_direct_peer(client_ns)     # test transport (real mode uses RPC)

server_ns.tick(delta)                    # call each frame on the server
```

Components are networked automatically: entities with at least one networked component (the default) are spawned, their dirty fields are pushed as deltas, and destroyed entities are despawned. Periodic reconciliation broadcasts a full state (anti-ghost).

## Editor remote monitor (debugger "ECS" tab)

While a game runs, the editor's debugger bottom panel gets an **"ECS"** tab that
shows the *running* game's live world — the ECS equivalent of Godot's Scene-tree
Remote mode. It works over `EngineDebugger`, so **start the game from the editor
with F5**; a standalone/headless run has no attached debugger (use the headless
CLI, the runtime overlay or MCP instead — see `docs/AI_DEBUGGING.md`).

### Steps

1. Press **F5** in the editor to run the game (a debug session must attach).
2. Open the bottom **Debugger** panel and switch to the **ECS** tab.
3. Click **Refresh** to pull one snapshot, or leave **"Auto refresh (1s)"** on
   (interval comes from `vortarisecs/debug/auto_refresh_interval`).
4. Browse the four pages below. Snapshots are **on-demand only** — the game never
   streams them every frame.

### The four pages

| Page | Shows |
|---|---|
| **Entities** | Entity id → component → field = value. Capped at `vortarisecs/general/max_snapshot_entities` (default 500); a `truncated (N/total)` note appears when the cap cut the list. |
| **Components** | Every registered component type: name, size, and per-field type / count / sync / networked. |
| **Systems** | Every registered system: name, group, active / paused, tick_interval, flush_mode. |
| **Stats** | `get_debug_stats()`: entity / archetype / component / observer counts, change_tick, pool size, query-cache entries. |

### Live field editing

Double-click any **Value** cell on the Entities page, type a new value and press
Enter. The editor sends `vecs:set_field` to the game, which applies it through
`VECSWorld.debug_set_field()`. The game **type-checks** the edit first: a value
whose Variant type does not match the field (e.g. a `Vector3` typed into an `F32`
cell) is rejected with a status-bar message instead of silently zeroing the field.

### Search, filter and sort

- Every page has a search box; typing filters immediately, clearing restores all.
  The Entities search adds a **mode dropdown**: **Mixed** (id + component +
  value substring), **By value** (field values; supports `comp/field == value`),
  **By component** (names only) and **Fuzzy** (loose subsequence matching).
- The Entities page also has a **Filter…** button that opens a component picker
  with an **All / Any** mode — show only entities that carry the selected
  components. It combines (AND) with the text search.
- Click a **column header** to sort that page's rows (ascending / descending
  toggle shown as `↑` / `↓`). Expanded/collapsed state survives refresh, sort and
  filter.

### Wire protocol

The extension registers an `EngineDebugger` message capture (prefix `vecs`) in
non-editor processes automatically — no demo code required:

| Channel | Direction | Data |
|---|---|---|
| `vecs:req_snapshot` | editor → game | `[]` |
| `vecs:snapshot` | game → editor | `[ <snapshot Dictionary> ]` |
| `vecs:set_field` | editor → game | `[entity_id, comp, field, value]` |
| `vecs:set_field_result` | game → editor | `[ok, entity_id, comp, field, error]` |

The snapshot Dictionary is built by `VECSWorld.get_snapshot_data()`:
`{ "protocol", "version", "stats", "components", "systems", "entities" }`.

> The separate editor **inspector dock**
> (`addons/vortarisecs/editor/ecs_inspector_dock.gd`) inspects the *editor*
> process's world, which is empty in a running game. For the live game use the
> debugger **ECS** tab (or the runtime overlay / headless CLI / MCP).

## Settings reference

All settings live under **Project Settings > VortarisECS** (the hierarchical
`vortarisecs/<category>/<name>` paths). Defaults are seeded only when absent, so
a value you set is never clobbered. The legacy flat `vortarisecs/verbose`
(0.3.0) is still honored as a fallback for `vortarisecs/general/verbose`.

| Setting | Default | What it does |
|---|---|---|
| `vortarisecs/general/verbose` | `false` | Tiered verbose logging (`[vortarisecs][v] …` traces: entity birth/death, component writes, observer dispatch, network packets, query execution). Debug builds only; release builds compile it out. `VECSWorld.set_verbose()` / `is_verbose()` read and write it. |
| `vortarisecs/general/auto_shutdown_on_exit` | `true` | Calls `VECSWorld.shutdown()` when the extension unloads, for a clean exit with no warnings/leaks. |
| `vortarisecs/general/max_snapshot_entities` | `500` | Entity cap for the editor's remote ECS monitor (`get_snapshot_data()` / `entities_to_data(max)`). Save-file serialization is never capped. |
| `vortarisecs/debug/auto_refresh_interval` | `1.0` s | Auto-refresh interval of the editor "ECS" debugger tab. |
| `vortarisecs/network/default_sync_priority` | `2` (Medium) | Default sync tier for fields registered from GDScript. Values: `0` Realtime (every tick), `1` High (20 Hz), `2` Medium (10 Hz), `3` Low (2 Hz), `4` SpawnOnly (once in the spawn packet), `5` Local (never networked). |
| `vortarisecs/observer/default_throttle_tick` | `0` | Default change-clock throttle for new observers' CHANGED delivery. `0` = off. |
| `vortarisecs/serialization/compact_json` | `false` | `true` = compact (unindented) JSON from `serialize_snapshot_json_string()`; `false` = pretty-printed with tab indentation. |

## Performance (Windows x64, 100k entities)

| Operation | Time |
|---|---|
| `for_each<Position, Velocity>` (C++) | **0.43 ms** |
| Query count | 0.02 ms |
| Create (via GDScript API) | 219 ms |
| Snapshot serialize / deserialize | 13 / 39 ms |

(Machine-measured baseline on this repo; prior numbers were 0.37 ms / 3.85 ms
before the per-row `is_alive` lookup was removed from the iteration hot path.)

## Layout

```
src/core/          pure C++ ECS core (no Godot objects)
src/reflect/       component schema macros + Variant conversions
src/serialization/ deterministic binary buffer, component codec, snapshot
src/network/       VECSNetworkSync + pluggable sync strategy
src/gdscript/      GDScript-facing classes (VECS prefix)
src/demo/          example components/systems compiled into the dll
demo/              Godot project (acceptance scene + perf test)
```
