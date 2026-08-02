#include "observer_dispatch.h"

#include <algorithm>

namespace vortaris {

namespace {
uint32_t event_bit(ObserverEventType p_type) {
	switch (p_type) {
		case ObserverEventType::Added:
			return EVENT_ADDED;
		case ObserverEventType::Removed:
			return EVENT_REMOVED;
		case ObserverEventType::Changed:
			return EVENT_CHANGED;
		case ObserverEventType::Matched:
			return EVENT_MATCHED;
		case ObserverEventType::Unmatched:
			return EVENT_UNMATCHED;
		case ObserverEventType::Custom:
			return EVENT_CUSTOM;
	}
	return 0;
}
} // namespace

ObserverId ObserverDispatch::add(ObserverCallback p_cb) {
	p_cb.id = next_id_++;
	callbacks_.push_back(std::move(p_cb));
	return p_cb.id;
}

void ObserverDispatch::remove(ObserverId p_id) {
	callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
			[&](const ObserverCallback &cb) { return cb.id == p_id; }),
			callbacks_.end());
}

void ObserverDispatch::dispatch(ObserverEventType p_type, Entity p_e, ComponentTypeId p_comp, const godot::String &p_event_name, const godot::Variant &p_payload) {
	if (callbacks_.empty()) {
		return;
	}
	uint32_t bit = event_bit(p_type);
	// COW snapshot: allows observers to register/unregister during delivery.
	std::vector<ObserverCallback> snapshot = callbacks_;
	for (const ObserverCallback &cb : snapshot) {
		if (!(cb.event_mask & bit)) {
			continue;
		}
		if (p_type == ObserverEventType::Custom) {
			if (!cb.custom_name.is_empty() && cb.custom_name != p_event_name) {
				continue;
			}
		} else if (!cb.watch_all && !cb.component_filter.empty()) {
			if (p_comp == 0 || std::find(cb.component_filter.begin(), cb.component_filter.end(), p_comp) == cb.component_filter.end()) {
				continue;
			}
		}
		if (cb.fn) {
			cb.fn(p_type, p_e, p_comp, p_event_name, p_payload);
		}
	}
}

} // namespace vortaris
