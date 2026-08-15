#pragma once

#include <godot_cpp/variant/string.hpp>

// Tiered logging helpers for VortarisECS, mirroring the ModLoader convention
// (`vortarismodloader/verbose` + `log_verbose`). Two tiers:
//
//   log_debug   — normal run log (world creation, component registration,
//                 snapshot save/load, network connection). Compiled out of
//                 release builds (#ifdef DEBUG_ENABLED), printed in debug.
//   log_verbose — detailed trace (entity birth/death, component writes,
//                 observer dispatch, network packet detail, query execution).
//                 Only printed in DEBUG_ENABLED builds AND when the
//                 `vortarisecs/verbose` project setting is true.
//
// Errors/warnings always go through push_error / push_warning and are NOT
// gated by these helpers.
namespace vortaris {

// True when detailed verbose logging is active: DEBUG_ENABLED build AND the
// `vortarisecs/verbose` project setting. The flag is cached so the hot-path
// check is a single bool load; use it to short-circuit message formatting:
//
//   if (vortaris::verbose_active()) {
//       vortaris::log_verbose("...");
//   }
//
// The cache is an inline variable so verbose_active() is a true single-load
// check (no function-call overhead) even from translation units that include
// this header — the hot path is never perturbed.
inline bool g_verbose_active = false;

inline bool verbose_active() {
#ifdef DEBUG_ENABLED
	return g_verbose_active;
#else
	return false;
#endif
}

// Sets the cached verbose flag AND the `vortarisecs/verbose` project setting
// so the runtime toggle and the persisted setting stay in sync. No-op in
// release builds (verbose logging is compiled out there).
void set_verbose(bool p_on);

// Re-reads the `vortarisecs/verbose` project setting into the cache. Called at
// module init so a value baked into project.godot seeds the flag.
void refresh_verbose();

// Normal run log: printed in DEBUG_ENABLED builds, compiled out in release.
void log_debug(const godot::String &p_msg);

// Detailed verbose log: printed only when verbose_active() is true.
void log_verbose(const godot::String &p_msg);

} // namespace vortaris
