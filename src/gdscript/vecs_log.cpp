#include "vecs_log.h"

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/print_string.hpp>

namespace vortaris {

void set_verbose(bool p_on) {
#ifdef DEBUG_ENABLED
	g_verbose_active = p_on;
	if (godot::ProjectSettings::get_singleton()) {
		godot::ProjectSettings::get_singleton()->set_setting("vortarisecs/verbose", p_on);
	}
#endif
}

void refresh_verbose() {
#ifdef DEBUG_ENABLED
	if (godot::ProjectSettings::get_singleton()) {
		g_verbose_active = godot::ProjectSettings::get_singleton()->get_setting("vortarisecs/verbose", false);
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
	if (g_verbose_active) {
		godot::print_line("[vortarisecs][v] " + p_msg);
	}
#endif
}

} // namespace vortaris
