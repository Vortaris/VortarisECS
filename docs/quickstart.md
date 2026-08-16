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

## 6. Typed field access (0.3.1)

`getf`/`setf` return a Variant, so the old idiom was `int(comp.get_field("hp"))`.
The typed getters read a field **directly as the requested type** — no casts:

```gdscript
var e: VECSEntity = w.spawn({"Combatant": {"hp": 30.0, "level": 5, "owner": 7}})
var hp: float = e.getf_float("Combatant", "hp")        # or e.get_component("Combatant").get_float("hp")
var lvl: int   = e.getf_int("Combatant", "level")
var alive: bool = e.getf_bool("Combatant", "is_alive")
var name: String = e.getf_string("Combatant", "name")
var pos: Variant = e.getf_vector("Combatant", "position")   # Vector2/3/4 (+ i variants)
```

Typed getters exist on both `VECSEntity` (`getf_int` / `getf_float` / `getf_bool`
/ `getf_string` / `getf_vector`) and `VECSComponent` (`get_int` / `get_float` /
`get_bool` / `get_string` / `get_vector`). A missing component/field returns the
type's zero value (`0` / `0.0` / `false` / `""` / null).

## 7. Field-equality query (0.3.1)

The old "find everything I own" scan was: query all `Combatant`, then loop over
the results comparing the `owner` field in GDScript. `field_equals` moves the
comparison into the query (C++ side, no per-entity callback):

```gdscript
var eid := 7
var owned: Array = w.query() \
    .with_all(["Combatant"]) \
    .field_equals("Combatant", "owner", eid) \
    .execute()          # also applied by execute_one() and count()
```

Each call adds one equality constraint; all must hold (AND). Equality follows
GDScript `==` semantics, so an I64 field matches an int, and an F32 `0.0` matches
an int `0`.

## 8. Spawn + observer conveniences (0.3.1)

```gdscript
# create_with_components: create + add components in one call, absent fields
# filled with schema defaults (0 / "" / false / zeroed array slots).
var unit: VECSEntity = w.create_with_components(0, {"Combatant": {"hp": 30.0}})
# def_id <= 0 auto-assigns; a positive def_id preassigns that id.

# on_changed: event-driven instead of polling hp every frame.
var changes := []
var obs: VECSObserver = w.on_changed("Combatant", {
    "fields": ["hp"],
    "callable": func(_ev: int, ent: VECSEntity, _p: Variant) -> void: changes.append(ent.get_id()),
})
unit.get_component("Combatant").set_field("hp", 31.0)   # fires the callback
w.remove_observer(obs)
obs.free()                                              # caller owns the observer

# Or build an observer without subclassing at all:
var obs2: VECSObserver = VECSObserver.new()
obs2.set_callback(func(event: int, entity: VECSEntity, payload: Variant) -> void: pass)
obs2.on_changed()
obs2.set_components(["Combatant"])
obs2.set_fields(["hp"])
w.add_observer(obs2)
```

`field_contains(name, value)` is the one-call array-membership check:
`comp.field_contains("tags", "burning")` is true when the scalar equals the
value or any element of a fixed-array field does.

## 9. Save and load

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

## Debugging a running game (0.2.x → 0.3.x)

Four ways to inspect the **live** world — full details in
[`AI_DEBUGGING.md`](AI_DEBUGGING.md):

- **Editor remote monitor** (0.3.0, recommended) — run the game from the editor
  with **F5**, open the bottom **Debugger** panel, switch to the **ECS** tab:
  - **Refresh** (or "Auto refresh (1s)") pulls a snapshot of the running game's
    world: Entities / Components / Systems / Stats pages.
  - **Live edit**: double-click a Value cell on the Entities page, type a new
    value and press Enter — the change is type-checked and applied to the running
    game (`VECSWorld.debug_set_field`).
  - **Search / filter / sort**: each page has a search box; the Entities page
    adds a mode dropdown (Mixed / By value / By component / Fuzzy) plus a
    component picker (All / Any); click column headers to sort.
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

> Process isolation: the editor **inspector dock** sees only the editor's own
> (empty) world. For the running game, use the debugger **ECS** tab, the overlay,
> the headless CLI, or MCP.

Also new: `world.set_verbose(true)` / `world.is_verbose()` toggle tiered verbose
logging (debug builds only, gated by the `vortarisecs/general/verbose` project
setting — with fallback to the legacy `vortarisecs/verbose` path from 0.3.0).
