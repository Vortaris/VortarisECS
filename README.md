# VortarisECS

**English** | [简体中文](README.zh-CN.md)

A modern, data-oriented ECS (Entity-Component-System) framework for **Godot 4.7** written in C++ as a GDExtension (godot-cpp). It is easy to use, robust, extensible and network-synchronizable.

Design reference: [GECS](https://github.com/BlockBreaker-Studios/GECS) — its archetype SoA storage, layered entity ids, deferred command buffer, query builder, observers and hierarchical network sync patterns were ported to native C++.

## Features

- **Pure C++ data components** — components are plain trivially-copyable structs stored in cache-friendly archetype columns (SoA). A declarative schema macro exposes fields for GDScript access and binary serialization.
- **Lightweight entity handles** — 64-bit layered ids (slot + generation); O(1) stale-handle detection; network preassigned ids.
- **Fast queries** — archetype membership in O(1); incrementally maintained query cache; `.changed()` change detection.
- **Deferred command buffer** — batched structural changes with a single archetype transition per entity and one cache invalidation per flush.
- **C++-first systems** — typed `world.for_each<Position, Velocity>` hot path with zero Variant overhead; group scheduling with dependency ordering, per-system timers and flush modes; GDScript systems via `_script_process`.
- **Observers / events** — ADDED / REMOVED / CHANGED / MATCHED / UNMATCHED / custom events, re-entrancy safe.
- **Deterministic binary serialization** — little-endian, fixed-width, byte-identical snapshots.
- **Pluggable network sync** — `VECSSyncStrategy` abstraction with a default server-authoritative snapshot replication (dirty-checked deltas + periodic reconciliation + anti-ghost). Transport is Godot's MultiplayerAPI (RPC), with a direct in-process test transport.

## Architecture

```
GDScript layer (VECS-prefixed classes)          C++ core (namespace vortaris)
─────────────────────────────────────           ─────────────────────────────
VECSWorld (Node / "VECS" singleton) ───────────► World
VECSEntity (RefCounted handle)      ───────────► Entity (64-bit layered id)
VECSComponent (field accessor)      ───────────► Archetype (SoA columns)
VECSQueryBuilder (fluent)           ───────────► Query / QueryCache
VECSCommandBuffer                   ───────────► CommandBuffer (deferred ops)
VECSSystem (Node, virtuals)         ───────────► SystemScheduler (groups/topo)
VECSObserver (Node, event hooks)    ───────────► ObserverDispatch
VECSBinaryBuffer / snapshot APIs    ───────────► BinaryBuffer / snapshot
VECSNetworkSync (Node, RPC)         ───────────► VECSSyncStrategy (pluggable)
                                                └─ VECSSnapshotReplication (default)
```

## Build (Windows / MSVC)

The plugin links against **godot-cpp** (external dependency). Get it first (must target Godot 4.7):

```bash
git clone -b 4.7 https://github.com/godotengine/godot-cpp.git godot-cpp
pip install scons
```

Then build the static library and the plugin (adjust `GODOT_CPP_PATH` in `SConstruct` to point at your checkout):

```bash
cd godot-cpp
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64   # for release exports
cd <this repo>
scons platform=windows target=template_debug arch=x86_64 build_library=False
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
(and runs as `demo/scripts/quickstart.gd`). The **convenience API** gets simple
things done in a few lines:

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

## JSON saves & data tables

Deeply integrated with Godot's own `JSON` class — pass in/out plain
`Dictionary` / `Array` / `String`, stringify/parse with the engine:

```gdscript
# World save (archive): serialize → stringify → write file
var text: String = JSON.stringify(world.serialize_snapshot_json(), "\t")
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
