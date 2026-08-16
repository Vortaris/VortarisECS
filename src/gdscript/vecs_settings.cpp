#include "vecs_settings.h"

#include <godot_cpp/classes/project_settings.hpp>

#include "../core/component_schema.h"

namespace vortaris {

namespace {
const char *k_verbose_new = "vortarisecs/general/verbose";
const char *k_verbose_old = "vortarisecs/verbose";
const char *k_auto_shutdown_on_exit = "vortarisecs/general/auto_shutdown_on_exit";
const char *k_max_snapshot_entities = "vortarisecs/general/max_snapshot_entities";
const char *k_auto_refresh_interval = "vortarisecs/debug/auto_refresh_interval";
const char *k_default_sync_priority = "vortarisecs/network/default_sync_priority";
const char *k_default_throttle_tick = "vortarisecs/observer/default_throttle_tick";
const char *k_compact_json = "vortarisecs/serialization/compact_json";
} // namespace

bool get_verbose_setting() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return false;
	}
	// Prefer the canonical hierarchical path; fall back to the legacy flat path
	// for projects that predate the 0.4.0 reorganization.
	if (ps->has_setting(godot::StringName(k_verbose_new))) {
		return (bool)ps->get_setting(k_verbose_new, false);
	}
	return (bool)ps->get_setting(k_verbose_old, false);
}

void set_verbose_setting(bool p_on) {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return;
	}
	ps->set_setting(k_verbose_new, p_on);
}

bool get_auto_shutdown_on_exit() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	return ps && (bool)ps->get_setting(k_auto_shutdown_on_exit, true);
}

int64_t get_max_snapshot_entities() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return 500;
	}
	const int64_t v = (int64_t)ps->get_setting(k_max_snapshot_entities, (int64_t)500);
	return v > 0 ? v : 1;
}

double get_auto_refresh_interval() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return 1.0;
	}
	const double v = (double)ps->get_setting(k_auto_refresh_interval, 1.0);
	return v > 0.0 ? v : 0.1;
}

uint8_t get_default_sync_priority() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return vortaris::SYNC_MEDIUM;
	}
	int64_t v = (int64_t)ps->get_setting(k_default_sync_priority, (int64_t)vortaris::SYNC_MEDIUM);
	if (v < (int64_t)vortaris::SYNC_REALTIME) {
		v = (int64_t)vortaris::SYNC_REALTIME;
	}
	if (v > (int64_t)vortaris::SYNC_LOCAL) {
		v = (int64_t)vortaris::SYNC_LOCAL;
	}
	return static_cast<uint8_t>(v);
}

int64_t get_default_throttle_tick() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	if (!ps) {
		return 0;
	}
	const int64_t v = (int64_t)ps->get_setting(k_default_throttle_tick, (int64_t)0);
	return v > 0 ? v : 0;
}

bool get_compact_json() {
	godot::ProjectSettings *ps = godot::ProjectSettings::get_singleton();
	return ps && (bool)ps->get_setting(k_compact_json, false);
}

} // namespace vortaris
