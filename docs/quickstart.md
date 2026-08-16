# VortarisECS Quickstart

This guide gets you from zero to a moving entity in about five minutes. It uses
the **convenience API** — the thin, beginner-friendly layer on top of the full
flexible ECS API. The two styles coexist: use the sugar for simple things, and
drop to the full API (`query()`, `commands()`, C++ `for_each`, ...) when you
need more control.

The complete script is at `demo/scripts/quickstart.gd`; run it with:

```
godot --headless --path demo --script res://scripts/quickstart.gd
```

## 1. Get the world

`VortarisECS` registers a global `VECS` singleton:

```gdscript
var w: VECSWorld = VECS.get_world()
```

## 2. Define components from a script

No C++ struct needed — declare the fields and the framework lays out the memory:

```gdscript
w.register_component("Pos", [
    {"name": "x", "type": "F32"},
    {"name": "y", "type": "F32"},
])
w.register_component("Vel", [{"name": "x", "type": "F32"}])
```

Field types: `Bool`, `I8..I64`, `U8..U64`, `F32/F64`, `Vector2..4(i)`, `Color`,
`Quaternion`, `Basis`, `Transform2D/3D`, `AABB`, `Rect2`, `Plane`,
`StringFixed` and `Blob`. Optional keys per field: `count` (arrays / fixed
strings), `sync_priority`, `networked`.

## 3. Spawn an entity in one call

```gdscript
var e := w.spawn({"Pos": {"x": 1.0, "y": 2.0}, "Vel": {"x": 0.5}})
```

## 4. Iterate over a component set

`each()` calls your lambda once per matching entity — no Array is built:

```gdscript
w.each(["Pos", "Vel"], func(ent: VECSEntity) -> void:
    ent.setf("Pos", "x", ent.getf("Pos", "x") + ent.getf("Vel", "x")))
```

`getf(comp, field)` / `setf(comp, field, value)` are one-call field accessors
for the common case. For more, `ent.get_component("Pos")` returns a
`VECSComponent` accessor with `get_field` / `set_field` / `get_fields`.

## 5. Query

```gdscript
var hits := w.query().with_all(["Pos"]).execute()   # Array of VECSEntity
var n := w.query().with_all(["Pos", "Vel"]).count()
var one := w.query().with_all(["Pos"]).execute_one()
```

## 6. Save and load

```gdscript
var text := JSON.stringify(w.serialize_snapshot_json(), "\t")
FileAccess.open("user://save.json", FileAccess.WRITE).store_string(text)

var ok := w.deserialize_snapshot_json(FileAccess.get_file_as_string("user://save.json"))
```

## The full API

When a system grows, reach for the rest of the toolbox — all documented in the
[README](../README.md):

- **Systems**: GDScript systems (`VECSSystem` + `_script_process`) or C++
  systems with `world.for_each<Position, Velocity>(...)` and cached `View` /
  change-aware `ChangeView`.
- **Deferred structural changes**: never add/remove components *inside* an
  iteration. Use `world.commands()` (a command buffer) so multiple changes to
  one entity collapse into a single archetype move. Structural changes issued
  during `for_each` / `each` / `View::each` are rejected with a loud error.
- **Events / observers**: ADDED / REMOVED / CHANGED / custom events via
  `VECSObserver`.
- **Networking**: bind a `VECSNetworkSync` to a world; dirty-checked deltas and
  periodic reconciliation are handled for you.

## Debugging a running game (0.2.x)

Three ways to inspect the **live** world (added in 0.2.0/0.2.1) — full details
in [`AI_DEBUGGING.md`](AI_DEBUGGING.md):

- **Headless CLI** (after the world is built, output is `[vortarisecs]`-prefixed):
  - `godot --headless --path demo -- --vortaris-ecs-stats` — print
    `get_debug_stats()` JSON and exit 0.
  - `godot --headless --path demo -- --vortaris-ecs-snapshot save.json` — export
    `serialize_snapshot_json_string()` to `user://` and exit 0.
- **Runtime overlay** (`--vortaris-ecs-overlay on`, or toggle with **F2** while
  the game runs) — an in-game HUD with live stats, an entity→component→field
  browser and JSON snapshot export/import.
- **MCP `run_script`** — call the plugin API directly inside a running game
  (`Engine.get_singleton("VECS").get_world()`).

Also new: `world.set_verbose(true)` / `world.is_verbose()` toggle tiered verbose
logging (debug builds only, gated by the `vortarisecs/general/verbose` project
setting — with fallback to the legacy `vortarisecs/verbose` path from 0.3.0).
