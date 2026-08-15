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
	// COW: snapshot, mutate a copy, publish a fresh immutable vector.
	std::vector<ObserverCallback> copy = callbacks_ ? *callbacks_ : std::vector<ObserverCallback>();
	copy.push_back(std::move(p_cb));
	callbacks_ = std::make_shared<const std::vector<ObserverCallback>>(std::move(copy));
	return p_cb.id;
}

void ObserverDispatch::clear() {
	callbacks_ = nullptr;
	next_id_ = 1;
}

void ObserverDispatch::remove(ObserverId p_id) {
	if (!callbacks_) {
		return;
	}
	std::vector<ObserverCallback> copy = *callbacks_;
	auto it = std::remove_if(copy.begin(), copy.end(),
			[&](const ObserverCallback &cb) { return cb.id == p_id; });
	if (it == copy.end()) {
		return; // not present; keep the shared snapshot untouched
	}
	copy.erase(it, copy.end());
	callbacks_ = std::make_shared<const std::vector<ObserverCallback>>(std::move(copy));
}

void ObserverDispatch::dispatch(ObserverEventType p_type, Entity p_e, ComponentTypeId p_comp, const godot::String &p_event_name, const godot::Variant &p_payload) {
	if (!callbacks_ || callbacks_->empty()) {
		return;
	}
	// COW snapshot: dispatch copies only the shared_ptr (atomic refcount), and
	// observers may register/unregister during delivery (re-entrancy safe).
	auto snapshot = callbacks_;
	uint32_t bit = event_bit(p_type);
	for (const ObserverCallback &cb : *snapshot) {
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
