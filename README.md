# VortarisECS

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

Output lands in `demo/bin/vortarisecs.windows.*.dll`. The first time you open `demo/` in Godot, the editor generates `.godot/extension_list.cfg` (this registers the extension).

Run the demo (functional + performance):

```
godot --headless --path demo
godot --headless --path demo --script res://scripts/perf_test.gd
```

## Quick start (GDScript)

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

## C++ systems

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
    void _run(vortaris::World &w, double delta) override {
        const float dt = float(delta);
        w.for_each<Position, Velocity>([&](vortaris::Entity e, Position &pos, Velocity &vel) {
            pos.x += vel.x * dt;
            pos.y += vel.y * dt;
            pos.z += vel.z * dt;
        });
    }
};
```

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
| `for_each<Position, Velocity>` (C++) | **0.37 ms** |
| Query count | 0.02 ms |
| Create (via GDScript API) | 219 ms |
| Snapshot serialize / deserialize | 13 / 39 ms |

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
