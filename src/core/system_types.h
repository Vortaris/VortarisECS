#pragma once

#include <cstdint>

#include <godot_cpp/variant/string.hpp>

namespace vortaris {

enum class SystemOrder : uint8_t {
	Any = 0,
	After = 1, // run after `target`
	Before = 2, // run before `target`
};

// A scheduling constraint relative to another system (matched by node name).
struct SystemDep {
	SystemOrder order = SystemOrder::Any;
	godot::String target;
};

enum FlushMode : int {
	FLUSH_PER_SYSTEM = 0, // flush command buffer right after this system
	FLUSH_PER_GROUP = 1,  // flush once after the whole group ran
	FLUSH_MANUAL = 2,     // never auto-flush
};

} // namespace vortaris
