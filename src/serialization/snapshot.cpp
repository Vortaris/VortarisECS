#include "snapshot.h"

#include <algorithm>
#include <unordered_set>

#include "../core/archetype.h"
#include "../core/component_registry.h"
#include "../core/entity.h"
#include "../core/world.h"
#include "component_serializer.h"

namespace vortaris {

void serialize_world_snapshot(const World &p_world, BinaryBuffer &r_out) {
	const ComponentRegistry &registry = ComponentRegistry::instance();

	// Deterministic order: sort archetypes by signature.
	std::vector<Archetype *> sorted = p_world.all_archetypes();
	std::sort(sorted.begin(), sorted.end(), [](const Archetype *a, const Archetype *b) {
		return a->signature < b->signature;
	});

	r_out.write_u16(SNAPSHOT_VERSION);
	r_out.write_u32(p_world.entity_count());

	for (const Archetype *a : sorted) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			Entity e = a->entities[row];
			r_out.write_u64(e.id);
			r_out.write_u16(static_cast<uint16_t>(a->component_ids.size()));
			for (size_t i = 0; i < a->component_ids.size(); ++i) {
				ComponentTypeId t = a->component_ids[i];
				const ComponentSchema *s = registry.schema_of(t);
				r_out.write_u32(t);
				serialize_component(*s, a->columns[i].row(row), r_out);
			}
		}
	}
}

bool deserialize_world_snapshot(World &p_world, BinaryBuffer &r_in) {
	const ComponentRegistry &registry = ComponentRegistry::instance();

	// --- Pass 1: validation-only walk (issue #4). A corrupt snapshot must not
	// erase the live world: the old code cleared first and then parsed, so a
	// failure mid-packet left an empty or half-populated world. Every read and
	// every id is checked here while the world stays untouched; only a fully
	// valid snapshot proceeds to pass 2 (where no read can fail).
	{
		BinaryBuffer in = r_in;
		uint16_t version;
		if (!in.read_u16(version) || version != SNAPSHOT_VERSION) {
			return false;
		}
		uint32_t count;
		if (!in.read_u32(count)) {
			return false;
		}
		std::unordered_set<uint64_t> ids;
		for (uint32_t i = 0; i < count; ++i) {
			uint64_t id;
			if (!in.read_u64(id)) {
				return false;
			}
			// Reject ids create_entity_preassigned would refuse (malformed slot /
			// beyond the runaway guard) and duplicate ids inside one snapshot.
			const uint32_t slot = static_cast<uint32_t>(id & 0xFFFFFFFFu) - 1;
			if (slot == 0xFFFFFFFFu || slot >= (1u << 24)) {
				return false;
			}
			if (!ids.insert(id).second) {
				return false;
			}
			uint16_t ncomp;
			if (!in.read_u16(ncomp)) {
				return false;
			}
			for (uint16_t j = 0; j < ncomp; ++j) {
				uint32_t t;
				if (!in.read_u32(t)) {
					return false;
				}
				const ComponentSchema *s = registry.schema_of(t);
				if (!s) {
					return false;
				}
				const size_t sz = serialized_component_size(*s);
				if (in.pos() + sz > in.size()) {
					return false;
				}
				in.seek(in.pos() + sz);
			}
		}
	}

	// --- Pass 2: apply. Loading a snapshot replaces the whole world; safe to
	// clear now that the payload is known-good.
	p_world.clear();

	uint16_t version;
	if (!r_in.read_u16(version) || version != SNAPSHOT_VERSION) {
		return false;
	}
	uint32_t count;
	if (!r_in.read_u32(count)) {
		return false;
	}

	for (uint32_t i = 0; i < count; ++i) {
		uint64_t id;
		if (!r_in.read_u64(id)) {
			return false;
		}
		Entity e = p_world.create_entity_preassigned(id);
		if (!e) {
			return false;
		}
		uint16_t ncomp;
		if (!r_in.read_u16(ncomp)) {
			return false;
		}
		for (uint16_t j = 0; j < ncomp; ++j) {
			uint32_t t;
			if (!r_in.read_u32(t)) {
				return false;
			}
			const ComponentSchema *s = registry.schema_of(t);
			if (!s) {
				return false;
			}
			p_world.add_raw(e, t, nullptr);
			void *dst = p_world.get_raw(e, t);
			if (!dst || !deserialize_component(*s, dst, r_in)) {
				return false;
			}
		}
	}
	return true;
}

} // namespace vortaris
