#include "vecs_log.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace vortaris {

namespace {
// Cached copy of the `vortarisecs/verbose` project setting. Keeping it as a
// plain bool makes the hot-path check (verbose_active) a single load instead of
// a ProjectSettings dictionary lookup on every call.
bool s_verbose = false;
} // namespace

bool verbose_active() {
#ifdef DEBUG_ENABLED
	return s_verbose;
#else
	return false;
#endif
}

void set_verbose(bool p_on) {
#ifdef DEBUG_ENABLED
	s_verbose = p_on;
	if (godot::ProjectSettings::get_singleton()) {
		godot::ProjectSettings::get_singleton()->set_setting("vortarisecs/verbose", p_on);
	}
#endif
}

void refresh_verbose() {
#ifdef DEBUG_ENABLED
	if (godot::ProjectSettings::get_singleton()) {
		s_verbose = godot::ProjectSettings::get_singleton()->get_setting("vortarisecs/verbose", false);
	}
#endif
}

void log_debug(const godot::String &p_msg) {
#ifdef DEBUG_ENABLED
	godot::print_line("[vortarisecs] " + p_msg);
#endif
}

void log_verbose(const godot::String &p_msg) {
#ifdef DEBUG_ENABLED
	if (s_verbose) {
		godot::print_line("[vortarisecs][v] " + p_msg);
	}
#endif
}

} // namespace vortaris
