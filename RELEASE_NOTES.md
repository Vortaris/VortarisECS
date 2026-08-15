# VortarisECS Release Notes

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
