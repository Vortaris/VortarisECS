# VortarisECS Release Notes

## 0.3.0 (2026-08-16)

**Project Settings reorganization + new tunables.** Settings moved from a flat
`vortarisecs/verbose` to a hierarchical `vortarisecs/<category>/<name>` layout
so they group nicely under Project Settings > VortarisECS, and several previously
hard-coded defaults became project settings.

### Project settings (hierarchical)

| Setting | Type / default | Effect |
|---|---|---|
| `vortarisecs/general/verbose` | bool, false | Detailed verbose logging (debug builds only). **Migrated from `vortarisecs/verbose`** (0.3.0) — the legacy flat path is still honored as a fallback. |
| `vortarisecs/general/auto_shutdown_on_exit` | bool, true | Whether extension teardown calls `VECSWorld::shutdown()` to clear transient state before the engine singleton is freed. |
| `vortarisecs/general/max_snapshot_entities` | int, 500 | Cap on the editor remote-monitor Entities page (rows rendered from a snapshot). |
| `vortarisecs/debug/auto_refresh_interval` | float, 1.0 | Seconds between auto-refresh requests in the editor "ECS" debugger tab. |
| `vortarisecs/network/default_sync_priority` | enum, Medium | Sync tier applied to new component fields that don't specify one. |
| `vortarisecs/observer/default_throttle_tick` | int, 0 | Default CHANGED throttle (change ticks) for newly created observers; 0 disables. |
| `vortarisecs/serialization/compact_json` | bool, false | Whether snapshot JSON strings (`serialize_snapshot_json_string()`) are written compact (true) or pretty-printed (false). |

All defaults are written only when the setting is absent, so a user's baked-in
value is never clobbered (mirrors the ModLoader F4 fix).

### API / docs / tests

- New public API: `VECSWorld.serialize_snapshot_json_string()` (doc_class +
  regression T36).
- `VECSWorld.is_verbose()` now reports the persisted setting (new path with
  legacy fallback) instead of the init-time cache.
- New regression T36 covering settings defaults, the registration guard, the
  verbose legacy-path fallback, and the effective reads of
  `default_sync_priority` / `default_throttle_tick` / `compact_json`.
- `plugin.cfg` version → `0.3.0`; README/README.zh-CN updated.

### Editor remote monitoring GUI (0.3.0)

**Runtime remote monitoring GUI.** The editor can now inspect a **running** game's
ECS world live, the same way Godot's scene tree Remote mode works — via an
"ECS" tab in the editor debugger bottom panel.

### Remote monitor (editor ↔ game over EngineDebugger)

- **Game side (C++)** — the extension registers an
  `EngineDebugger` message capture with prefix `vecs` (in non-editor processes
  only). On `vecs:req_snapshot` it answers `vecs:snapshot` carrying a JSON-able
  Dictionary from the new `VECSWorld.get_snapshot_data()`.
- **Snapshot format** — `{ "protocol", "version", "stats", "components",
  "systems", "entities" }`: `stats` is `get_debug_stats()`, `components` lists
  every registered component type (name / id / size / fields with
  type-count-sync_priority-networked), `systems` lists every registered system
  (name / group / active / paused / tick_interval / flush_mode), `entities` is
  `entities_to_data()`.
- **Editor side (GDScript)** — new `EditorDebuggerPlugin`
  (`addons/vortarisecs/editor/ecs_debugger_plugin.gd`) adds an **"ECS"** tab to
  the debugger bottom panel while a game runs. The tab has four pages:
  **Entities** (id → component → field = value), **Components** (registry with
  field metadata), **Systems** (name/group/active/paused) and **Stats**
  (world counters), plus a Refresh button and an optional ~1 Hz auto-refresh.
  The entity browser caps display at 500 entities.
- **On-demand only** — snapshots are sent when the editor asks (refresh click /
  auto-refresh / session start), never every frame.
- **Difference vs the inspector dock** — the existing
  `editor/ecs_inspector_dock.gd` (editor dock, right panel) can only see the
  *editor* process's empty world; the new debugger tab sees the *running* game.
  Both are documented in `docs/AI_DEBUGGING.md`.

### API / docs / tests

- New public API: `VECSWorld.get_snapshot_data()` (doc_class + regression T35).
- `SystemScheduler` gained `collect_systems()` (internal) to enumerate systems
  for the snapshot.
- `plugin.cfg` version → `0.3.0`; README/README.zh-CN updated.

## 0.2.1 (2026-08-15)

Patch release focused on **debuggability**: an in-game runtime overlay, a
headless/AI-friendly CLI, tiered logging, and AI-facing documentation.

### Fixes
- **Runtime overlay replaces the editor dock for debugging.** The editor
  inspector dock runs in the editor process, which cannot see a world built
  inside a running (F5) game because of process isolation. A new in-game
  runtime overlay (`addons/vortarisecs/ecs_overlay.gd` + `ecs_overlay.tscn`)
  shows live stats, an entity/component/field browser and JSON snapshot
  export/import for the **real** running world. Off by default; enable with
  `--vortaris-ecs-overlay on` or toggle with **F2** in-game.
- Editor dock now documents its limitation (editor-side world only).

### CLI
- The demo bootstrap parses `--vortaris-*` user args **after** building the
  world, so stats reflect the real game world:
  - `--vortaris-ecs-stats` — print `get_debug_stats()` JSON, exit 0.
  - `--vortaris-ecs-snapshot <path>` — export `serialize_snapshot_json()` to
    `user://` or an explicit path, exit 0 (1 on write failure).
  - `--vortaris-ecs-overlay on|off` — toggle the runtime overlay, keep running.
  - Unknown arguments print usage and exit 1.
- Output is prefixed `[vortarisecs]` for machine readability. Full table in
  `docs/AI_DEBUGGING.md`.

### Logging
- New project setting `vortarisecs/verbose` (bool, default false).
- Tiered logging helpers (`log_debug` / `log_verbose`, mirroring the ModLoader
  `vortarismodloader/verbose` convention):
  - Normal run logs (world creation, component registration, snapshot save/load,
    network connect) print in **debug** builds only.
  - Detailed traces (entity birth/death, component writes, observer dispatch,
    network packet detail, query execution) require **debug + verbose**.
  - Errors/warnings are unaffected at all levels.
- Hot paths are short-circuited with a cached `verbose_active()` bool; log
  strings are only formatted when verbose is on. Release builds compile the
  helpers out entirely.
- New public API: `VECSWorld.set_verbose(bool)` / `VECSWorld.is_verbose()`.

### Docs
- New `docs/AI_DEBUGGING.md`: MCP `run_script` plugin-API examples, the headless
  CLI parameter table with exit codes, the runtime overlay usage, and the
  editor-dock isolation warning. Linked from the READMEs.

## 0.2.0 (2026-08-14)

### Features
- Entity lookup (`entity(id)` / `has_entity(id)`); `get_component` returns a
  null handle when a component is not attached.
- Field defaults for `get_field` / `getf` / `VECSComponent.get_field`.
- Query ergonomics: `find_by_components`, `where`, `order_by`, `order_by_id`.
- Fixed-array fields (`get_field_count` / `get_array_element` /
  `set_array_element`; array type metadata on `VECSComponentType`).
- `StringFixed` writes are truncated to the fixed capacity on a UTF-8 code-point
  boundary (whole characters only, with a warning); `count == 0` stores an empty
  string.
- Id mapping (`spawn_from_data_mapped` / `deserialize_snapshot_json_mapped` /
  `remap_reference`) and `"parent"` auto-parenting.
- Entity pooling (`create_entity_pooled` / `destroy_entity_pooled` /
  `pool_size`).
- Cross-world copy/merge (`copy_entity_to` / `merge_world`).
- Event bus (`emit_event` receive count, `subscribe_event` /
  `unsubscribe_event`, value-compared `on_field_changed` + `off`).
- Observer field filters and change-tick throttling.

### Fixes (GitHub issues)
- `issue#1` ChangeView optimization: per-column max-version fast path +
  incremental world write-log collection (same results, much faster).
- `issue#2` `sync_priority` delta throttling (REALTIME / HIGH 20 Hz /
  MEDIUM 10 Hz / LOW 2 Hz).
- `issue#3` / `issue#4` packet validation before any write (truncated,
  unknown-schema, id-conflict packets are dropped with no partial state);
  clean shutdown removing all exit-time warnings/leaks.
- Networking hardening, deterministic serialization, 64-bit change clock,
  entity-generation wrap-around guard.

### Tooling
- `get_debug_stats()`, `VECSQueryBuilder.get_last_execution_time_usec()`,
  editor inspector dock.
- `VECSWorld.shutdown()` — resets transient state (deferred ops, change
  baselines, observer dispatch, scheduler) so a world can be reused; also run at
  extension teardown to remove exit-time warnings/leaks.
