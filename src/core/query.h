#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "component_type.h"

namespace vortaris {

struct Archetype;

// A compiled query: entity must have all `all`, at least one of `any` (if
// non-empty) and none of `none`. The membership signature is a FNV-1a hash of
// the sorted term sets; queries sharing a signature match the same archetype
// set, which is what the QueryCache keys on.
struct Query {
	std::vector<ComponentTypeId> all;
	std::vector<ComponentTypeId> any;
	std::vector<ComponentTypeId> none;

	uint64_t membership_signature() const;
	bool matches(const Archetype &p_a) const;
};

// Incrementally-maintained archetype->query cache. New archetypes are tested
// against every cached query and appended to the matching entries; destroyed
// archetypes are erased. Entities moving between archetypes never invalidate
// this cache (only the per-entity result cache version does).
class QueryCache {
public:
	const std::vector<Archetype *> &match(Query &p_q, const std::vector<Archetype *> &p_archetypes);
	void register_new_archetype(Archetype *p_a);
	void erase_archetype(Archetype *p_a);
	void invalidate();
	// Number of distinct compiled queries currently cached (debug/stats).
	size_t cached_query_count() const { return cache_.size(); }

private:
	std::unordered_map<uint64_t, std::vector<Archetype *>> cache_;
	std::unordered_map<uint64_t, Query> cached_queries_;
};

} // namespace vortaris
