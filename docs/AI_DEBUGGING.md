# AI Debugging Guide for VortarisECS

This guide is written for AI agents (and humans) debugging a **running** VortarisECS
game. It covers four ways to inspect the live ECS world:

1. **Editor remote monitor** — an "ECS" tab in the editor debugger bottom panel
   that shows the running game's world live (EditorDebuggerPlugin, "Remote" mode).
2. **MCP `run_script`** — call the plugin API directly from inside the running game.
3. **Headless CLI arguments** — one-shot stats / snapshot dumps via the demo bootstrap.
4. **Runtime overlay** — an in-game HUD for interactive browsing.

> **Important (process isolation).** The editor inspector dock
> (`addons/vortarisecs/editor/ecs_inspector_dock.gd`) runs in the **editor**
> process, which has its own (empty) VortarisECS world. It **cannot** see a world
> built inside a running (F5) game — that is the whole reason the runtime overlay
> and the remote debugger tab exist. To debug a running game, use the **editor
> debugger tab** (start the game with F5), the runtime overlay, the headless CLI
> arguments, or MCP `run_script` — never the editor dock.

---

## 0. Editor remote monitor (debugger "ECS" tab)

The editor ships an `EditorDebuggerPlugin` (see
`addons/vortarisecs/editor/ecs_debugger_plugin.gd` and
`ecs_debugger_tab.gd`) that adds an **"ECS"** tab to the debugger bottom panel
while a game runs:

1. Run the game from the editor (**F5** — a debug session must be attached).
2. Open the bottom **Debugger** panel and switch to the **ECS** tab.
3. Use **Refresh** (or leave **Auto refresh (1s)** on) to fetch a fresh snapshot.

The tab has four pages rendered from the snapshot Dictionary:

| Page | Shows |
|---|---|
| Entities | entity id → component → field = value (capped at 500 entities) |
| Components | every registered component type: name, size, and per-field type / count / sync_priority / networked |
| Systems | every registered system: name, group, active / paused, tick_interval, flush_mode |
| Stats | `get_debug_stats()`: entity / archetype / component / observer counts, change_tick, pool size, query cache |

### How it works (wire protocol)

The game side registers an `EngineDebugger` message capture with prefix `vecs`
(the extension does this automatically in non-editor processes — no demo code
required). Four channels:

| Channel | Direction | Data |
|---|---|---|
| `vecs:req_snapshot` | editor → game | `[]` |
| `vecs:snapshot` | game → editor | `[ <snapshot Dictionary> ]` |
| `vecs:set_field` | editor → game | `[entity_id, comp, field, value]` |
| `vecs:set_field_result` | game → editor | `[ok, entity_id, comp, field, error]` |

The snapshot Dictionary comes from `VECSWorld.get_snapshot_data()`:

```json
{
  "protocol": 1,
  "version": 5,
  "stats":   { "entity_count": 10, "archetype_count": 8, "component_count": 7,
               "observer_count": 0, "change_tick": 54, "pool_size": 0,
               "query_cache_entries": 3 },
  "components": [ { "name": "Position", "id": 1, "size": 12,
                    "fields": [ { "name": "x", "type": "F32", "count": 1,
                                  "sync_priority": 2, "networked": true } ] } ],
  "systems": [ { "name": "MoveSystem", "group": "physics", "active": true,
                 "paused": false, "tick_interval": 0.0, "flush_mode": 0 } ],
  "entities": [ { "id": 1, "components": { "Position": { "x": 1.0 } } } ]
}
```

- `stats` is the same dictionary as `get_debug_stats()`.
- `entities` is `entities_to_data(max_entities)` — capped at
  `vortarisecs/general/max_snapshot_entities` (default 500). When the cap cuts
  the list, the snapshot also carries `"truncated": true` and `"entity_total"`
  (the real world count); save-file serialization (`serialize_snapshot_json`)
  never truncates.
- Snapshots are **on-demand only** — sent when the editor asks, never every
  frame. `set_field` writes go through `VECSWorld.debug_set_field()`, which
  validates the entity/component/field and **type-checks** the value before
  applying it (a mismatch returns `{"ok": false, "error": "type mismatch ..."}`
  instead of silently zeroing the field).

### Live editing, search, filter, sort (the tab's UX)

- **Live value editing**: double-click a **Value** cell on the Entities page,
  type and press Enter. The editor sends `vecs:set_field`; the game replies on
  `vecs:set_field_result`. A rejected edit (type mismatch, dead entity, unknown
  component/field) shows the error in the tab's status bar and the cell is
  refreshed from the world's real value.
- **Search**: every page has a search box (instant filter). The Entities search
  has a mode dropdown:
  - **Mixed** — id + component + value substring.
  - **By value** — field values; supports `comp/field == value` (e.g.
    `Combatant/hp == 30`).
  - **By component** — component names only.
  - **Fuzzy** — loose subsequence matching.
- **Filter**: the Entities page's **Filter…** button opens a component picker
  with an **All / Any** mode — show only entities carrying the selected
  components. It ANDs with the text search.
- **Sort**: clicking a column header sorts that page's top-level rows
  (ascending / descending toggle, `↑`/`↓` glyph). Expanded/collapsed state
  survives refresh, sort and filter.

### Settings that affect the monitor

| Setting | Default | Effect |
|---|---|---|
| `vortarisecs/general/max_snapshot_entities` | `500` | Caps the Entities page rows / snapshot `entities` array. |
| `vortarisecs/debug/auto_refresh_interval` | `1.0` s | Auto-refresh timer interval (the "Auto refresh" checkbox). |

Both are read fresh each time the tab is built, so editing them in Project
Settings takes effect on the next editor session.

---

## 2. MCP `run_script` (Godot MCP)

The Godot MCP server can execute arbitrary GDScript inside the running project.
Scripts must `extends RefCounted` and define `func execute(scene_tree) -> Variant`.

### Get the live world

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	return world.entity_count()
```

`Engine.get_singleton("VECS")` is the `VECSWorld` node registered as the `VECS`
engine singleton. `world.get_world()` returns the same node (they are identical).

### Read debug statistics

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	return world.get_debug_stats()
# → {"entity_count": 10, "archetype_count": 8, "component_count": 7,
#    "observer_count": 0, "change_tick": 54, "pool_size": 0, "query_cache_entries": 3}
```

### Query entities

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var q := world.query().with_all(["Position", "Velocity"])
	var hits: Array = q.execute()
	return {
		"count": hits.size(),
		"last_exec_usec": q.get_last_execution_time_usec(),
		"first_id": hits[0].get_id() if hits.size() > 0 else null,
	}
```

Fluent query terms: `with_all`, `with_any`, `with_none`, `changed`, `enabled`,
`where`, `order_by`, `order_by_id`, terminated by `execute()` / `execute_one()` /
`count()`.

### Read / write component fields

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var e: VECSEntity = world.entity(1)                 # by raw id, or query()
	if e == null:
		return {"error": "entity 1 not alive"}
	var comp: VECSComponent = e.get_component("Position")
	return {
		"fields": comp.get_fields(),                    # whole Dictionary
		"x": comp.get_field("x"),                       # one field
		"x_again": world.get_field(e, "Position", "x"), # world-level sugar
	}
```

### Dump / load a JSON snapshot

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var save: Dictionary = world.serialize_snapshot_json()   # {version, entities:[...]}
	var text: String = JSON.stringify(save, "\t")
	var path := "user://mcp_snapshot.json"
	FileAccess.open(path, FileAccess.WRITE).store_string(text)

	# ... later, or in a second call:
	var ok: bool = world.deserialize_snapshot_json(FileAccess.get_file_as_string(path))
	return {"saved": path, "bytes": text.length(), "load_ok": ok}
```

### Toggle verbose logging

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	world.set_verbose(true)   # also writes the vortarisecs/general/verbose project setting
	return world.is_verbose()
```

Verbose traces (`[vortarisecs][v] ...`) appear in `get_debug_output` /
stdout. They are only emitted in **debug** builds (`template_debug`); release
builds compile them out entirely.

### Subscribe to component changes (observer, 0.3.1)

`on_changed` is a one-call replacement for per-frame polling: the callback fires
only when the watched field(s) actually changed.

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var hits := []
	var obs: VECSObserver = world.on_changed("Combatant", {
		"fields": ["hp"],
		"callable": func(_ev: int, ent: VECSEntity, _p: Variant) -> void:
			hits.append(ent.get_id()),
	})
	# ... later, force a change to observe the callback firing:
	var some: Array = world.query().with_all(["Combatant"]).execute()
	if some.size() > 0:
		(some[0] as VECSEntity).get_component("Combatant").set_field("hp", 999.0)
	await scene_tree.process_frame
	world.remove_observer(obs)
	obs.free()
	return {"callback_fired_on": hits}
```

Or build the observer explicitly — `VECSObserver.new()` works since 0.3.1:

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var obs: VECSObserver = VECSObserver.new()
	obs.set_callback(func(event: int, entity: VECSEntity, payload: Variant) -> void:
		print("event=", event, " entity=", entity.get_id()))
	obs.on_added()
	obs.on_changed()
	obs.set_components(["Combatant"])
	obs.set_throttle_tick(2)
	world.add_observer(obs)
	return obs.get_world().get_debug_stats()["observer_count"]
```

### Write a field with type checking (`debug_set_field`)

`world.debug_set_field(eid, comp, field, value)` is the same code path the
editor's live edit uses. It validates the entity/component/field and rejects a
value whose Variant type does not match the field:

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var e: VECSEntity = world.query().with_all(["Combatant"]).execute_one()
	if e == null:
		return {"error": "no Combatant"}
	return {
		"good": world.debug_set_field(e.get_id(), "Combatant", "hp", 42.0),
		"bad":  world.debug_set_field(e.get_id(), "Combatant", "hp", "not a float"),
	}
# → {"good": {"ok": true, "error": ""}, "bad": {"ok": false, "error": "type mismatch ..."}}
```

### Dump the full remote-monitor snapshot

```gdscript
extends RefCounted

func execute(scene_tree: SceneTree) -> Variant:
	var world: VECSWorld = Engine.get_singleton("VECS").get_world()
	var snap: Dictionary = world.get_snapshot_data()
	return {
		"protocol": snap.get("protocol"),
		"stats": snap.get("stats"),
		"entity_count_shown": (snap.get("entities", []) as Array).size(),
		"truncated": snap.get("truncated", false),
		"entity_total": snap.get("entity_total", 0),
	}
```

---

## 3. Headless CLI arguments

The demo bootstrap (`demo/scripts/main.gd`) parses `OS.get_cmdline_user_args()`
(the arguments after `--`). **Actions run AFTER the demo builds the world**, so
stats and snapshots reflect the real game world. Output lines are prefixed with
`[vortarisecs]` for machine readability.

| Argument | Effect | Exit code |
|---|---|---|
| `--vortaris-ecs-stats` | Prints `get_debug_stats()` as JSON, quits. | 0 |
| `--vortaris-ecs-snapshot <path>` | Exports `serialize_snapshot_json_string()` (honors `vortarisecs/serialization/compact_json`) to `<path>` (relative paths land in `user://`), prints the destination, quits. | 0 on success, 1 on write failure |
| `--vortaris-ecs-overlay on\|off` | Enables/disables the runtime overlay, **keeps the game running** (no quit). | runs until the game exits |
| anything else | Prints usage and quits. | 1 |

Examples (all from the repo root):

```bash
# World statistics as JSON
godot --headless --path demo -- --vortaris-ecs-stats

# Save a JSON snapshot to the user data dir
godot --headless --path demo -- --vortaris-ecs-snapshot save.json

# Save into a subdirectory under the user data dir; parent directories that do
# not exist yet are created automatically (UTF-8 byte count is reported)
godot --headless --path demo -- --vortaris-ecs-snapshot user://saves/save.json

# Launch the game with the runtime overlay ON (headless or windowed)
godot --path demo -- --vortaris-ecs-overlay on
```

> Snapshot paths: prefer `user://` or an absolute filesystem path — missing
> parent directories are created automatically. `res://` is read-only after the
> project is exported, so writing a snapshot there fails; do not use it.

Sample `--vortaris-ecs-stats` output:

```
[vortarisecs] stats {"archetype_count":8,"change_tick":54,"component_count":7,"entity_count":10,"observer_count":0,"pool_size":0,"query_cache_entries":3}
```

Sample `--vortaris-ecs-snapshot` output:

```
[vortarisecs] snapshot saved to user://save.json (1666 bytes, 10 entities)
```

> Windows note: `godot --headless --path demo -- --vortaris-ecs-snapshot C:\tmp\save.json`
> is treated as an absolute path (drive-letter detection), not `user://`-prefixed.

---

## 4. Runtime overlay

The runtime overlay (`addons/vortarisecs/ecs_overlay.gd` + `ecs_overlay.tscn`) is
a `CanvasLayer` HUD that lives **inside the game process** and shows the real,
live world:

- **Stats** — entity / archetype / component / observer counts, `change_tick`,
  and a sampled query execution time (`get_debug_stats()` +
  `VECSQueryBuilder.get_last_execution_time_usec()`).
- **Browser** — "Refresh" lists every entity → component → field as an expandable
  tree (`world.entities_to_data()`).
- **Snapshot** — "Export Snapshot" writes
  `serialize_snapshot_json_string()` (honoring
  `vortarisecs/serialization/compact_json`) to
  `user://vortarisecs_snapshot.json`; "Import Snapshot" loads it back.

**Off by default.** Enable it with the startup argument
`godot --path demo -- --vortaris-ecs-overlay on`, or at runtime by instantiating
the scene and calling `set_overlay_enabled(true)`. Press **F2** while the game
runs to toggle it.

The overlay is a demo/plugin file, not part of the C++ core; you can drop it into
your own project's `addons/vortarisecs/` and instantiate it from your main scene.

---

## Cheat sheet: VECS singleton API used while debugging

| Call | What it returns |
|---|---|
| `VECS.get_world()` | the `VECSWorld` node |
| `world.get_debug_stats()` | `{entity_count, archetype_count, component_count, observer_count, change_tick, pool_size, query_cache_entries}` |
| `world.get_snapshot_data()` | the editor-monitor snapshot `{protocol, version, stats, components, systems, entities}` (entities capped at `max_snapshot_entities`, with `truncated`/`entity_total`) |
| `world.entity_count()` / `world.entity(id)` / `world.has_entity(id)` | count / live-entity lookup |
| `world.entities_to_data(max_entities := 0)` | `[{id, components:{Name:{field:value}}}]`; `max_entities > 0` caps the export (0 = all) |
| `world.serialize_snapshot_json()` / `world.deserialize_snapshot_json(x)` | JSON save / load (Dictionary or JSON String) |
| `world.serialize_snapshot()` / `world.deserialize_snapshot(bytes)` | deterministic binary save / load |
| `world.query().with_all([...]).changed([...]).execute()` | matched `VECSEntity[]` |
| `world.query().with_all([...]).field_equals("Comp","field",v).execute()` | C++-side field-equality filter (0.3.1) |
| `builder.get_last_execution_time_usec()` | µs of the last `execute()` |
| `world.debug_set_field(eid, comp, field, value)` | `{"ok": bool, "error": String}` — type-checked runtime write |
| `world.on_changed(comp, opts)` / `world.create_observer(callable, opts)` | a registered `VECSObserver` (0.3.1) |
| `world.create_with_components(def_id, comps)` | spawn + add components, schema defaults filled (0.3.1) |
| `ent.getf_int(comp, field)` / `getf_float` / `getf_bool` / `getf_string` / `getf_vector` | typed one-call field read (0.3.1) |
| `comp.get_int(field)` / `get_float` / `get_bool` / `get_string` / `get_vector` | typed field read on a `VECSComponent` (0.3.1) |
| `comp.field_contains(name, value)` | scalar-equals or any-array-element check (0.3.1) |
| `world.set_verbose(true)` / `world.is_verbose()` | toggle / query verbose logging |

See `docs/quickstart.md` and the `doc_classes/*.xml` class reference for the full
API surface.
