#include "query.h"

#include <algorithm>

#include "archetype.h"

namespace vortaris {

uint64_t Query::membership_signature() const {
	uint64_t h = 14695981039346656037ull;
	auto mix = [&h](std::vector<ComponentTypeId> ids, uint8_t p_tag) {
		std::sort(ids.begin(), ids.end());
		h ^= p_tag;
		h *= 1099511628211ull;
		for (ComponentTypeId id : ids) {
			uint32_t v = id;
			for (int i = 0; i < 4; ++i) {
				h ^= (v & 0xFF);
				h *= 1099511628211ull;
				v >>= 8;
			}
		}
	};
	mix(all, 1);
	mix(any, 2);
	mix(none, 3);
	return h;
}

bool Query::matches(const Archetype &p_a) const {
	for (ComponentTypeId id : all) {
		if (!p_a.has_component(id)) {
			return false;
		}
	}
	if (!any.empty()) {
		bool ok = false;
		for (ComponentTypeId id : any) {
			if (p_a.has_component(id)) {
				ok = true;
				break;
			}
		}
		if (!ok) {
			return false;
		}
	}
	for (ComponentTypeId id : none) {
		if (p_a.has_component(id)) {
			return false;
		}
	}
	return true;
}

const std::vector<Archetype *> &QueryCache::match(Query &p_q, const std::vector<Archetype *> &p_archetypes) {
	uint64_t key = p_q.membership_signature();
	auto it = cache_.find(key);
	if (it != cache_.end()) {
		return it->second;
	}
	std::vector<Archetype *> result;
	result.reserve(p_archetypes.size());
	for (Archetype *a : p_archetypes) {
		if (p_q.matches(*a)) {
			result.push_back(a);
		}
	}
	cached_queries_[key] = p_q;
	auto &entry = cache_[key];
	entry = std::move(result);
	return entry;
}

void QueryCache::register_new_archetype(Archetype *p_a) {
	for (auto &kv : cached_queries_) {
		if (kv.second.matches(*p_a)) {
			cache_[kv.first].push_back(p_a);
		}
	}
}

void QueryCache::erase_archetype(Archetype *p_a) {
	for (auto &kv : cache_) {
		std::vector<Archetype *> &vec = kv.second;
		vec.erase(std::remove(vec.begin(), vec.end(), p_a), vec.end());
	}
}

void QueryCache::invalidate() {
	cache_.clear();
	cached_queries_.clear();
}

} // namespace vortaris
