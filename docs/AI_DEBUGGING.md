# AI Debugging Guide for VortarisECS

This guide is written for AI agents (and humans) debugging a **running** VortarisECS
game. It covers three ways to inspect the live ECS world:

1. **MCP `run_script`** — call the plugin API directly from inside the running game.
2. **Headless CLI arguments** — one-shot stats / snapshot dumps via the demo bootstrap.
3. **Runtime overlay** — an in-game HUD for interactive browsing.

> **Important (process isolation).** The editor inspector dock
> (`addons/vortarisecs/editor/ecs_inspector_dock.gd`) runs in the **editor**
> process, which has its own (empty) VortarisECS world. It **cannot** see a world
> built inside a running (F5) game — that is the whole reason the runtime overlay
> exists. To debug a running game, use the runtime overlay, the headless CLI
> arguments, or MCP `run_script` — never the editor dock.

---

## 1. MCP `run_script` (Godot MCP)

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
	world.set_verbose(true)   # also writes the vortarisecs/verbose project setting
	return world.is_verbose()
```

Verbose traces (`[vortarisecs][v] ...`) appear in `get_debug_output` /
stdout. They are only emitted in **debug** builds (`template_debug`); release
builds compile them out entirely.

---

## 2. Headless CLI arguments

The demo bootstrap (`demo/scripts/main.gd`) parses `OS.get_cmdline_user_args()`
(the arguments after `--`). **Actions run AFTER the demo builds the world**, so
stats and snapshots reflect the real game world. Output lines are prefixed with
`[vortarisecs]` for machine readability.

| Argument | Effect | Exit code |
|---|---|---|
| `--vortaris-ecs-stats` | Prints `get_debug_stats()` as JSON, quits. | 0 |
| `--vortaris-ecs-snapshot <path>` | Exports `serialize_snapshot_json()` to `<path>` (relative paths land in `user://`), prints the destination, quits. | 0 on success, 1 on write failure |
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

## 3. Runtime overlay

The runtime overlay (`addons/vortarisecs/ecs_overlay.gd` + `ecs_overlay.tscn`) is
a `CanvasLayer` HUD that lives **inside the game process** and shows the real,
live world:

- **Stats** — entity / archetype / component / observer counts, `change_tick`,
  and a sampled query execution time (`get_debug_stats()` +
  `VECSQueryBuilder.get_last_execution_time_usec()`).
- **Browser** — "Refresh" lists every entity → component → field as an expandable
  tree (`world.entities_to_data()`).
- **Snapshot** — "Export Snapshot" writes `serialize_snapshot_json()` to
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
| `world.entity_count()` / `world.entity(id)` / `world.has_entity(id)` | count / live-entity lookup |
| `world.entities_to_data()` | `[{id, components:{Name:{field:value}}}]` |
| `world.serialize_snapshot_json()` / `world.deserialize_snapshot_json(x)` | JSON save / load (Dictionary or JSON String) |
| `world.serialize_snapshot()` / `world.deserialize_snapshot(bytes)` | deterministic binary save / load |
| `world.query().with_all([...]).changed([...]).execute()` | matched `VECSEntity[]` |
| `builder.get_last_execution_time_usec()` | µs of the last `execute()` |
| `world.set_verbose(true)` / `world.is_verbose()` | toggle / query verbose logging |

See `docs/quickstart.md` and the `doc_classes/*.xml` class reference for the full
API surface.
