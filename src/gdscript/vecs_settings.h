#pragma once

#include <cstdint>

// Hierarchical project settings for VortarisECS. All settings live under
// `vortarisecs/<category>/<name>` so they group nicely in the Project Settings
// editor (Project Settings > VortarisECS). Accessors supply a sensible default
// when the setting is absent; the verbose flag additionally falls back to the
// pre-0.4.0 flat path so upgraded projects keep working.
namespace vortaris {

// Reads the verbose-logging flag from `vortarisecs/general/verbose`, falling
// back to the legacy `vortarisecs/verbose` path (0.3.0) for projects that have
// not been migrated yet. Returns false when no setting exists.
bool get_verbose_setting();

// Persists the verbose flag to the canonical `vortarisecs/general/verbose` path
// so `set_verbose` and the persisted setting stay in sync.
void set_verbose_setting(bool p_on);

// `vortarisecs/general/auto_shutdown_on_exit` (default true): whether module
// teardown explicitly calls VECSWorld::shutdown() to clear transient state
// before the engine singleton is freed.
bool get_auto_shutdown_on_exit();

// `vortarisecs/general/max_snapshot_entities` (default 500): the cap the
// editor's remote-monitor Entities page renders from a snapshot.
int64_t get_max_snapshot_entities();

// `vortarisecs/debug/auto_refresh_interval` (default 1.0): seconds between
// auto-refresh requests in the editor's remote-monitor tab.
double get_auto_refresh_interval();

// `vortarisecs/network/default_sync_priority` (default SYNC_MEDIUM): the sync
// tier applied to new component fields that don't specify one. Clamped to the
// valid SyncPriority range.
uint8_t get_default_sync_priority();

// `vortarisecs/observer/default_throttle_tick` (default 0): the default CHANGED
// throttle (in change ticks) for newly created observers. 0 disables throttling.
int64_t get_default_throttle_tick();

// `vortarisecs/serialization/compact_json` (default false): whether snapshot
// JSON strings are written compactly (true) or pretty-printed with tabs (false).
bool get_compact_json();

} // namespace vortaris
