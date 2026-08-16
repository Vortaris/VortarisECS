#include "vecs_log.h"

#include <godot_cpp/core/print_string.hpp>

#include "vecs_settings.h"

namespace vortaris {

void set_verbose(bool p_on) {
#ifdef DEBUG_ENABLED
	g_verbose_active = p_on;
	vortaris::set_verbose_setting(p_on);
#endif
}

void refresh_verbose() {
#ifdef DEBUG_ENABLED
	g_verbose_active = vortaris::get_verbose_setting();
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
