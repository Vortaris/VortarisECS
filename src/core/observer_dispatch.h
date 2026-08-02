#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "component_type.h"
#include "entity.h"

namespace vortaris {

enum class ObserverEventType : uint8_t {
	Added = 0,
	Removed = 1,
	Changed = 2,
	Matched = 3,
	Unmatched = 4,
	Custom = 5,
};

constexpr uint32_t EVENT_ADDED = 1u << 0;
constexpr uint32_t EVENT_REMOVED = 1u << 1;
constexpr uint32_t EVENT_CHANGED = 1u << 2;
constexpr uint32_t EVENT_MATCHED = 1u << 3;
constexpr uint32_t EVENT_UNMATCHED = 1u << 4;
constexpr uint32_t EVENT_CUSTOM = 1u << 5;

using ObserverId = int;

struct ObserverCallback {
	using Fn = std::function<void(ObserverEventType, Entity, ComponentTypeId, const godot::String &, const godot::Variant &)>;
	ObserverId id = 0;
	Fn fn;
	uint32_t event_mask = 0;
	std::vector<ComponentTypeId> component_filter; // non-empty => only these types
	bool watch_all = true;
	godot::String custom_name; // non-empty => only custom events with this name
};

// Synchronous event dispatch for observers. Callbacks are stored in a COW
// snapshot so an observer may register/unregister other observers while an
// event is being delivered (re-entrancy safe).
class ObserverDispatch {
public:
	ObserverId add(ObserverCallback p_cb);
	void remove(ObserverId p_id);
	void dispatch(ObserverEventType p_type, Entity p_e, ComponentTypeId p_comp, const godot::String &p_event_name, const godot::Variant &p_payload);
	bool is_empty() const { return callbacks_.empty(); }

private:
	std::vector<ObserverCallback> callbacks_;
	int next_id_ = 1;
};

} // namespace vortaris
