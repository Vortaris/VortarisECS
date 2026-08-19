#include "network_sync.h"

#include <algorithm>

#include <godot_cpp/variant/dictionary.hpp>

#include "../core/component_registry.h"
#include "../core/world.h"
#include "../gdscript/vecs_log.h"
#include "../serialization/component_serializer.h"
#include "gdscript/vecs_world.h"

using namespace godot;

namespace {
// Wire format version. v2 (0.4.0) serializes components with explicit field
// masks: SYNC_LOCAL fields never cross the wire and delta packets carry only
// the sync_priority buckets that are due (issue #2). Receivers still accept
// v1 packets (legacy full-component blocks) so mixed-version sessions degrade
// gracefully instead of dropping traffic.
constexpr uint16_t SYNC_VERSION = 2;
constexpr uint16_t SYNC_VERSION_MIN = 1;
constexpr int RPC_MODE_ANY_PEER = 2;

void patch_u32(vortaris::BinaryBuffer &p_buf, size_t p_pos, uint32_t p_value) {
	p_buf.overwrite_u32(p_pos, p_value);
}

// Dry-runs a packet with a read-only cursor: checks the sync version, every
// entity/component count, that each type id maps to a registered schema, and
// that every component block fits in the remaining bytes. Returns false on any
// malformed reference so the caller can drop the packet BEFORE touching the
// world — a truncated or schema-mismatched packet must never leave a partially
// applied state.
// Seconds between delta sends for a sync priority tier. Returns -1 for tiers
// that never participate in deltas (SPAWN_ONLY / LOCAL).
double priority_interval(uint8_t p_priority) {
	switch (p_priority) {
		case vortaris::SYNC_REALTIME: return 0.0;
		case vortaris::SYNC_HIGH: return 0.05;
		case vortaris::SYNC_MEDIUM: return 0.1;
		case vortaris::SYNC_LOW: return 0.5;
		default: return -1.0;
	}
}

// Effective delta-send interval of a component: the FASTEST (smallest) interval
// among its networked fields. A component whose only non-local fields are
// SPAWN_ONLY is sent once in the spawn packet and never in deltas (-1).
double component_interval(vortaris::ComponentTypeId p_t) {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_t);
	if (!s) {
		return -1.0;
	}
	double fastest = -1.0;
	for (const auto &f : s->fields) {
		if (!f.is_networked) {
			continue;
		}
		const double iv = priority_interval(f.sync_priority);
		if (iv < 0.0) {
			continue; // spawn-only / local fields create no delta obligation
		}
		if (fastest < 0.0 || iv < fastest) {
			fastest = iv;
		}
	}
	return fastest;
}

// Wire v2 (issue #2): per-schema field-level replication plan.
//  - spawn_mask: every networked field except SYNC_LOCAL (spawn / full-state
//    packets replicate these; SYNC_LOCAL never crosses the wire).
//  - buckets: one entry per DISTINCT delta interval among the fields, ordered
//    by ascending interval (bucket 0 = fastest). Each carries the mask of the
//    fields it replicates. Deltas send only the buckets that are due, so a
//    REALTIME position + LOW inventory component no longer ships its inventory
//    at REALTIME frequency.
// Both peers derive the plan from the same schema with this same code, so
// bucket indices and masks agree without any negotiation.
struct FieldSyncPlan {
	std::vector<bool> spawn_mask;
	std::vector<double> bucket_intervals;
	std::vector<std::vector<bool>> bucket_masks;
	bool any_delta = false;
};

const FieldSyncPlan &sync_plan_for(vortaris::ComponentTypeId p_t) {
	static std::unordered_map<vortaris::ComponentTypeId, FieldSyncPlan> plans;
	auto it = plans.find(p_t);
	if (it != plans.end()) {
		return it->second;
	}
	FieldSyncPlan plan;
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_t);
	if (s) {
		plan.spawn_mask.assign(s->fields.size(), false);
		std::vector<double> intervals;
		for (const auto &f : s->fields) {
			const bool net = f.is_networked && f.sync_priority != vortaris::SYNC_LOCAL;
			const double iv = net ? priority_interval(f.sync_priority) : -1.0;
			if (net) {
				// SPAWN_ONLY (iv < 0) fields ride spawn/full-state only; the
				// spawn mask still includes them.
				plan.spawn_mask[&f - s->fields.data()] = true;
			}
			if (iv >= 0.0 && std::find(intervals.begin(), intervals.end(), iv) == intervals.end()) {
				intervals.push_back(iv);
			}
		}
		std::sort(intervals.begin(), intervals.end());
		for (double iv : intervals) {
			std::vector<bool> mask(s->fields.size(), false);
			size_t idx = 0;
			for (const auto &f : s->fields) {
				const bool net = f.is_networked && f.sync_priority != vortaris::SYNC_LOCAL;
				if (net && priority_interval(f.sync_priority) == iv) {
					mask[idx] = true;
				}
				++idx;
			}
			plan.bucket_intervals.push_back(iv);
			plan.bucket_masks.push_back(std::move(mask));
		}
		plan.any_delta = !plan.bucket_intervals.empty();
	}
	return plans.emplace(p_t, std::move(plan)).first->second;
}

void write_field_mask(vortaris::BinaryBuffer &p_buf, const std::vector<bool> &p_mask) {
	const size_t bytes = vortaris::field_mask_bytes(p_mask.size());
	for (size_t b = 0; b < bytes; ++b) {
		uint8_t bits = 0;
		for (size_t bit = 0; bit < 8; ++bit) {
			const size_t i = b * 8 + bit;
			if (i < p_mask.size() && p_mask[i]) {
				bits |= static_cast<uint8_t>(1u << bit);
			}
		}
		p_buf.write_u8(bits);
	}
}

bool read_field_mask(vortaris::BinaryBuffer &p_buf, size_t p_field_count, std::vector<bool> &r_mask) {
	r_mask.assign(p_field_count, false);
	const size_t bytes = vortaris::field_mask_bytes(p_field_count);
	for (size_t b = 0; b < bytes; ++b) {
		uint8_t bits;
		if (!p_buf.read_u8(bits)) {
			return false;
		}
		for (size_t bit = 0; bit < 8; ++bit) {
			const size_t i = b * 8 + bit;
			if (i < p_field_count && (bits & (1u << bit)) != 0) {
				r_mask[i] = true;
			}
		}
	}
	return true;
}

bool validate_packet(const vortaris::BinaryBuffer &p_data, SyncPacketKind p_kind) {
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);

	// Despawn packets carry no version header — just the 8-byte entity id.
	if (p_kind == SyncPacketKind::Despawn) {
		uint64_t id;
		if (!in.read_u64(id)) {
			return false;
		}
		return in.at_end();
	}

	uint16_t version;
	if (!in.read_u16(version) || version < SYNC_VERSION_MIN || version > SYNC_VERSION) {
		return false;
	}

	// Consumes one component payload. v1: fixed whole-component block.
	// v2 spawn/full-state shape: field mask + masked field bytes (issue #2).
	auto consume_component = [&in, version](uint32_t p_type) -> bool {
		const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_type);
		if (!s) {
			return false;
		}
		size_t sz;
		if (version == 1) {
			sz = vortaris::serialized_component_size(*s);
		} else {
			std::vector<bool> mask;
			if (!read_field_mask(in, s->fields.size(), mask)) {
				return false;
			}
			sz = vortaris::serialized_component_fields_size(*s, mask);
		}
		if (in.pos() + sz > in.size()) {
			return false;
		}
		in.seek(in.pos() + sz);
		return true;
	};

	// Consumes one v2 delta component record set: u8 record count, then per
	// record bucket index + field mask + masked payload.
	auto consume_delta_component_v2 = [&in](uint32_t p_type) -> bool {
		const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_type);
		if (!s) {
			return false;
		}
		uint8_t nrec;
		if (!in.read_u8(nrec)) {
			return false;
		}
		for (uint8_t r = 0; r < nrec; ++r) {
			uint8_t bidx;
			std::vector<bool> mask;
			if (!in.read_u8(bidx) || !read_field_mask(in, s->fields.size(), mask)) {
				return false;
			}
			const size_t sz = vortaris::serialized_component_fields_size(*s, mask);
			if (in.pos() + sz > in.size()) {
				return false;
			}
			in.seek(in.pos() + sz);
		}
		return true;
	};

	if (p_kind == SyncPacketKind::Spawn) {
		uint64_t id;
		if (!in.read_u64(id)) {
			return false;
		}
		uint16_t ncomp;
		if (!in.read_u16(ncomp)) {
			return false;
		}
		for (uint16_t i = 0; i < ncomp; ++i) {
			uint32_t t;
			if (!in.read_u32(t) || !consume_component(t)) {
				return false;
			}
		}
		return in.at_end();
	}

	// Delta (u8 component count) and FullState (u16 component count).
	const bool is_delta = p_kind == SyncPacketKind::Delta;
	if (!is_delta && p_kind != SyncPacketKind::FullState) {
		return false; // unknown kind
	}
	uint32_t n;
	if (!in.read_u32(n)) {
		return false;
	}
	for (uint32_t i = 0; i < n; ++i) {
		uint64_t id;
		if (!in.read_u64(id)) {
			return false;
		}
		if (is_delta) {
			uint8_t ncomp;
			if (!in.read_u8(ncomp)) {
				return false;
			}
			for (uint8_t j = 0; j < ncomp; ++j) {
				uint32_t t;
				if (!in.read_u32(t)) {
					return false;
				}
				const bool ok = (version == 1) ? consume_component(t) : consume_delta_component_v2(t);
				if (!ok) {
					return false;
				}
			}
		} else {
			uint16_t ncomp;
			if (!in.read_u16(ncomp)) {
				return false;
			}
			for (uint16_t j = 0; j < ncomp; ++j) {
				uint32_t t;
				if (!in.read_u32(t) || !consume_component(t)) {
					return false;
				}
			}
		}
	}
	return in.at_end();
}
} // namespace

// ------------------------------------------------------------------ strategy --

void VECSSyncStrategy::_bind_methods() {
}

// ------------------------------------------------- snapshot replication ----

bool VECSSnapshotReplication::is_networked(vortaris::ComponentTypeId p_t) {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_t);
	if (!s || !s->is_networked) {
		return false;
	}
	// SYNC_LOCAL fields never replicate; a component whose fields are all
	// SYNC_LOCAL is treated as not networked at all.
	for (const auto &f : s->fields) {
		if (f.sync_priority != vortaris::SYNC_LOCAL) {
			return true;
		}
	}
	return false;
}

void VECSSnapshotReplication::serialize_component_block(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t, vortaris::BinaryBuffer &r_buf) {
	const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(p_t);
	if (!s) {
		return;
	}
	const void *data = p_ns.world()->core().get_raw(p_e, p_t);
	if (!data) {
		return;
	}
	// Wire v2 spawn/full-state component record:
	//   u32 type id | field mask (spawn set) | masked field bytes
	// The mask makes SYNC_LOCAL fields unreachable from the wire (issue #2 bug:
	// v1 serialized the whole component, leaking them) and keeps the reader
	// robust if a schema gains fields.
	const FieldSyncPlan &plan = sync_plan_for(p_t);
	r_buf.write_u32(p_t);
	write_field_mask(r_buf, plan.spawn_mask);
	vortaris::serialize_component_fields(*s, data, r_buf, plan.spawn_mask);
}

void VECSSnapshotReplication::on_entity_component_added(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {
	if (!p_ns.is_server() || p_ns.is_applying() || !is_networked(p_t)) {
		return;
	}
	if (tracked_.insert(p_e).second) {
		pending_spawn_.insert(p_e);
	} else {
		dirty_[p_e].insert(p_t);
	}
}

void VECSSnapshotReplication::on_entity_component_changed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {
	if (!p_ns.is_server() || p_ns.is_applying() || !is_networked(p_t)) {
		return;
	}
	if (tracked_.count(p_e) > 0) {
		dirty_[p_e].insert(p_t);
	}
}

void VECSSnapshotReplication::on_entity_component_removed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {
	if (!p_ns.is_server() || p_ns.is_applying() || !is_networked(p_t)) {
		return;
	}
	auto dit = dirty_.find(p_e);
	if (dit != dirty_.end()) {
		dit->second.erase(p_t);
	}
	if (tracked_.count(p_e) > 0) {
		std::vector<vortaris::ComponentTypeId> comps;
		p_ns.world()->core().get_entity_component_types(p_e, comps);
		bool any = false;
		for (vortaris::ComponentTypeId c : comps) {
			if (is_networked(c)) {
				any = true;
				break;
			}
		}
		if (!any) {
			tracked_.erase(p_e);
			dirty_.erase(p_e);
			// Audit fix: the per-entity throttle state must die with the entity,
			// otherwise next_send_tick_ grows without bound over long runs.
			next_send_tick_.erase(p_e);
			pending_despawn_.insert(p_e);
		}
	}
}

void VECSSnapshotReplication::seed_from_world(VECSNetworkSync &p_ns) {
	if (!p_ns.is_server()) {
		return;
	}
	vortaris::World &w = p_ns.world()->core();
	for (const vortaris::Archetype *a : w.all_archetypes()) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			bool any = false;
			for (vortaris::ComponentTypeId t : a->component_ids) {
				if (is_networked(t)) {
					any = true;
					break;
				}
			}
			if (any) {
				// Only newly-tracked entities are queued for spawn, so repeated
				// seeding (e.g. set_strategy at runtime) does not respawn them.
				if (tracked_.insert(a->entities[row]).second) {
					pending_spawn_.insert(a->entities[row]);
				}
			}
		}
	}
}

void VECSSnapshotReplication::build_delta(VECSNetworkSync &p_ns, vortaris::BinaryBuffer &r_out) {
	vortaris::World &w = p_ns.world()->core();
	r_out.write_u16(SYNC_VERSION);
	size_t count_pos = r_out.pos();
	r_out.write_u32(0);
	uint32_t n = 0;

	for (auto it = dirty_.begin(); it != dirty_.end();) {
		vortaris::Entity e = it->first;
		if (!w.is_alive(e)) {
			it = dirty_.erase(it);
			continue;
		}
		vortaris::BinaryBuffer comp_buf;
		uint8_t ncomp = 0;
		auto &comp_set = it->second;
		for (auto cit = comp_set.begin(); cit != comp_set.end();) {
			const vortaris::ComponentTypeId t = *cit;
			if (!is_networked(t) || !w.has(e, t)) {
				cit = comp_set.erase(cit);
				continue;
			}
			// Wire v2 field-level throttle (issue #2): each sync_priority bucket
			// of the component has its own cadence. Due buckets are serialized
			// now; not-yet-due buckets keep the component dirty so they are
			// retried on a later tick. Components with no delta-eligible fields
			// (spawn-only / local) never appear in deltas.
			const FieldSyncPlan &plan = sync_plan_for(t);
			if (!plan.any_delta) {
				cit = comp_set.erase(cit); // spawn-only / local: never in a delta
				continue;
			}
			std::vector<double> &ticks = next_send_tick_[e][t];
			if (ticks.size() < plan.bucket_intervals.size()) {
				ticks.resize(plan.bucket_intervals.size(), 0.0);
			}
			const void *data = w.get_raw(e, t);
			const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
			vortaris::BinaryBuffer rec_buf;
			uint8_t nrec = 0;
			bool all_sent = true;
			for (size_t b = 0; b < plan.bucket_intervals.size(); ++b) {
				if (elapsed_time_ < ticks[b]) {
					all_sent = false; // not due yet: retry later
					continue;
				}
				rec_buf.write_u8(static_cast<uint8_t>(b));
				write_field_mask(rec_buf, plan.bucket_masks[b]);
				vortaris::serialize_component_fields(*s, data, rec_buf, plan.bucket_masks[b]);
				ticks[b] = elapsed_time_ + plan.bucket_intervals[b];
				++nrec;
			}
			if (nrec > 0) {
				comp_buf.write_u32(t);
				comp_buf.write_u8(nrec);
				comp_buf.write_bytes(rec_buf.data(), rec_buf.size());
				++ncomp;
			}
			if (all_sent) {
				cit = comp_set.erase(cit);
			} else {
				++cit; // some buckets still pending: stay dirty
			}
		}
		const bool emptied = comp_set.empty();
		if (ncomp > 0) {
			r_out.write_u64(e.id);
			r_out.write_u8(ncomp);
			r_out.write_bytes(comp_buf.data(), comp_buf.size());
			++n;
		}
		if (emptied) {
			it = dirty_.erase(it);
		} else {
			++it;
		}
	}

	if (n > 0) {
		patch_u32(r_out, count_pos, n);
	} else {
		r_out.clear();
	}
}

void VECSSnapshotReplication::serialize_full_state(VECSNetworkSync &p_ns, vortaris::BinaryBuffer &r_out) {
	vortaris::World &w = p_ns.world()->core();
	std::vector<vortaris::Archetype *> sorted = w.all_archetypes();
	std::sort(sorted.begin(), sorted.end(), [](const vortaris::Archetype *a, const vortaris::Archetype *b) {
		return a->signature < b->signature;
	});
	r_out.write_u16(SYNC_VERSION);
	size_t count_pos = r_out.pos();
	r_out.write_u32(0);
	uint32_t n = 0;
	for (const vortaris::Archetype *a : sorted) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			vortaris::Entity e = a->entities[row];
			vortaris::BinaryBuffer comp_buf;
			uint16_t ncomp = 0;
			for (size_t i = 0; i < a->component_ids.size(); ++i) {
				vortaris::ComponentTypeId t = a->component_ids[i];
				if (is_networked(t)) {
					// v2 spawn-format record (type + mask + masked fields); the
					// mask keeps SYNC_LOCAL bytes off the wire.
					serialize_component_block(p_ns, e, t, comp_buf);
					++ncomp;
				}
			}
			if (ncomp == 0) {
				continue;
			}
			r_out.write_u64(e.id);
			r_out.write_u16(ncomp);
			r_out.write_bytes(comp_buf.data(), comp_buf.size());
			++n;
		}
	}
	if (n > 0) {
		patch_u32(r_out, count_pos, n);
	} else {
		r_out.clear();
	}
}

void VECSSnapshotReplication::tick(VECSNetworkSync &p_ns, double p_delta) {
	if (!p_ns.is_server()) {
		return;
	}
	++tick_count_;
	elapsed_time_ += p_delta;
	vortaris::World &w = p_ns.world()->core();

	// Spawns (delayed one tick so multi-component entities spawn complete).
	if (!pending_spawn_.empty()) {
		size_t spawned = 0;
		for (vortaris::Entity e : pending_spawn_) {
			if (!w.is_alive(e)) {
				continue;
			}
			vortaris::BinaryBuffer buf;
			buf.write_u16(SYNC_VERSION);
			buf.write_u64(e.id);
			std::vector<vortaris::ComponentTypeId> comps;
			w.get_entity_component_types(e, comps);
			vortaris::BinaryBuffer comp_buf;
			uint16_t ncomp = 0;
			for (vortaris::ComponentTypeId t : comps) {
				if (is_networked(t)) {
					serialize_component_block(p_ns, e, t, comp_buf);
					++ncomp;
				}
			}
			buf.write_u16(ncomp);
			buf.write_bytes(comp_buf.data(), comp_buf.size());
			p_ns.send_packet(SyncPacketKind::Spawn, buf);
			++spawned;
		}
		pending_spawn_.clear();
		if (vortaris::verbose_active()) {
			vortaris::log_verbose("network spawn " + godot::String::num_int64(static_cast<int64_t>(spawned)) + " entities");
		}
	}

	// Deltas (dirty networked components). build_delta removes the components it
	// actually sent; components that were throttled (not yet due) stay dirty so
	// they are retried on a later tick.
	if (!dirty_.empty()) {
		vortaris::BinaryBuffer buf;
		build_delta(p_ns, buf);
		if (buf.size() > 0) {
			p_ns.send_packet(SyncPacketKind::Delta, buf);
			if (vortaris::verbose_active()) {
				vortaris::log_verbose("network delta packet (" + godot::String::num_int64(static_cast<int64_t>(buf.size())) + " bytes)");
			}
		}
	}

	// Despawns. The entity may already be dead locally — that is the point:
	// notify peers to remove their copy.
	if (!pending_despawn_.empty()) {
		for (vortaris::Entity e : pending_despawn_) {
			vortaris::BinaryBuffer buf;
			buf.write_u64(e.id);
			p_ns.send_packet(SyncPacketKind::Despawn, buf);
			// Audit fix: release throttle state for good once the despawn goes out.
			next_send_tick_.erase(e);
		}
		if (vortaris::verbose_active()) {
			vortaris::log_verbose("network despawn " + godot::String::num_int64(static_cast<int64_t>(pending_despawn_.size())) + " entities");
		}
		pending_despawn_.clear();
	}

	// Periodic reconciliation (full state).
	reconcile_accum_ += p_delta;
	if (reconcile_accum_ >= reconciliation_interval_) {
		reconcile_accum_ = 0.0;
		vortaris::BinaryBuffer buf;
		serialize_full_state(p_ns, buf);
		if (buf.size() > 0) {
			p_ns.send_packet(SyncPacketKind::FullState, buf);
			if (vortaris::verbose_active()) {
				vortaris::log_verbose("network full_state packet (" + godot::String::num_int64(static_cast<int64_t>(buf.size())) + " bytes)");
			}
		}
	}
}

void VECSSnapshotReplication::send_full_state(VECSNetworkSync &p_ns, int64_t p_peer) {
	if (!p_ns.is_server()) {
		return;
	}
	vortaris::BinaryBuffer buf;
	serialize_full_state(p_ns, buf);
	if (buf.size() > 0) {
		p_ns.send_packet(SyncPacketKind::FullState, buf, p_peer);
	}
}

void VECSSnapshotReplication::reset_state() {
	// The world was replaced wholesale (snapshot load) — drop every tracked id
	// so no stale handles linger in the dirty/spawn/despawn sets.
	tracked_.clear();
	dirty_.clear();
	pending_spawn_.clear();
	pending_despawn_.clear();
	client_replicated_.clear();
	tick_count_ = 0;
	reconcile_accum_ = 0.0;
	elapsed_time_ = 0.0;
	next_send_tick_.clear();
}

void VECSSnapshotReplication::apply_spawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint16_t version;
	if (!in.read_u16(version) || version < SYNC_VERSION_MIN || version > SYNC_VERSION) {
		return;
	}
	uint64_t id;
	if (!in.read_u64(id)) {
		return;
	}
	vortaris::Entity e = w.create_entity_preassigned(id);
	if (!e) {
		return;
	}
	uint16_t ncomp;
	if (!in.read_u16(ncomp)) {
		return;
	}
	for (uint16_t i = 0; i < ncomp; ++i) {
		uint32_t t;
		if (!in.read_u32(t)) {
			return;
		}
		const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
		if (!s) {
			return;
		}
		w.add_raw(e, t, nullptr);
		void *dst = w.get_raw(e, t);
		if (!dst) {
			return;
		}
		if (version == 1) {
			if (!vortaris::deserialize_component(*s, dst, in)) {
				return;
			}
		} else {
			std::vector<bool> mask;
			if (!read_field_mask(in, s->fields.size(), mask) ||
					!vortaris::deserialize_component_fields(*s, dst, in, mask)) {
				return;
			}
		}
	}
	client_replicated_.insert(e);
}

void VECSSnapshotReplication::apply_delta(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint16_t version;
	if (!in.read_u16(version) || version < SYNC_VERSION_MIN || version > SYNC_VERSION) {
		return;
	}
	uint32_t n;
	if (!in.read_u32(n)) {
		return;
	}
	for (uint32_t i = 0; i < n; ++i) {
		uint64_t id;
		if (!in.read_u64(id)) {
			return;
		}
		vortaris::Entity e{ id };
		uint8_t ncomp;
		if (!in.read_u8(ncomp)) {
			return;
		}
		for (uint8_t j = 0; j < ncomp; ++j) {
			uint32_t t;
			if (!in.read_u32(t)) {
				return;
			}
			const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
			if (!s) {
				return;
			}
			if (version == 1) {
				// Legacy whole-component block.
				if (!w.is_alive(e)) {
					// The entity died locally (e.g. despawned while this delta was
					// in flight). We still must consume the component's bytes so
					// the read cursor stays aligned for the remaining entries.
					static thread_local std::vector<uint8_t> scratch;
					scratch.assign(s->size, 0);
					if (!vortaris::deserialize_component(*s, scratch.data(), in)) {
						return;
					}
					continue;
				}
				w.add_raw(e, t, nullptr);
				void *dst = w.get_raw(e, t);
				if (!dst || !vortaris::deserialize_component(*s, dst, in)) {
					return;
				}
			} else {
				// Wire v2: one record per due sync-priority bucket; each record
				// carries its field mask. Unmasked fields keep their current
				// value — that is the point of field-level throttling.
				uint8_t nrec;
				if (!in.read_u8(nrec)) {
					return;
				}
				if (!w.is_alive(e)) {
					// Dead locally: consume mask + masked payload without writing.
					static thread_local std::vector<uint8_t> scratch;
					for (uint8_t r = 0; r < nrec; ++r) {
						uint8_t bidx;
						std::vector<bool> mask;
						if (!in.read_u8(bidx) || !read_field_mask(in, s->fields.size(), mask)) {
							return;
						}
						const size_t sz = vortaris::serialized_component_fields_size(*s, mask);
						if (in.pos() + sz > in.size()) {
							return;
						}
						if (scratch.size() < sz) {
							scratch.resize(sz);
						}
						if (!vortaris::deserialize_component_fields(*s, scratch.data(), in, mask)) {
							return;
						}
					}
					continue;
				}
				if (!w.has(e, t)) {
					w.add_raw(e, t, nullptr);
				}
				void *dst = w.get_raw(e, t);
				if (!dst) {
					return;
				}
				for (uint8_t r = 0; r < nrec; ++r) {
					uint8_t bidx;
					std::vector<bool> mask;
					if (!in.read_u8(bidx) || !read_field_mask(in, s->fields.size(), mask) ||
							!vortaris::deserialize_component_fields(*s, dst, in, mask)) {
						return;
					}
				}
			}
		}
		if (w.is_alive(e)) {
			client_replicated_.insert(e);
		}
	}
}

void VECSSnapshotReplication::apply_full_state(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint16_t version;
	if (!in.read_u16(version) || version < SYNC_VERSION_MIN || version > SYNC_VERSION) {
		return;
	}
	uint32_t n;
	if (!in.read_u32(n)) {
		return;
	}
	// Pass 1 (issue #4): pre-flight every entity id BEFORE mutating the world.
	// validate_packet already guarantees structure/byte-bounds/schema presence,
	// so the only mid-apply failure left was create_entity_preassigned refusing
	// an id (malformed slot / runaway guard) — by which point earlier entities
	// in the packet had already been destroyed+rebuilt, leaving a half-applied
	// world. Checking all ids up front makes the apply phase uninterruptible.
	{
		vortaris::BinaryBuffer scan = in;
		std::unordered_set<uint64_t> seen;
		for (uint32_t i = 0; i < n; ++i) {
			uint64_t id;
			if (!scan.read_u64(id)) {
				return;
			}
			const uint32_t slot = static_cast<uint32_t>(id & 0xFFFFFFFFu) - 1;
			if (slot == 0xFFFFFFFFu || slot >= (1u << 24)) {
				return; // create_entity_preassigned would refuse this id
			}
			if (!seen.insert(id).second) {
				// Duplicate id inside one packet: sequential apply would
				// destroy+rebuild it twice, but the intent is ambiguous — reject
				// the whole packet instead of guessing.
				return;
			}
			uint16_t ncomp;
			if (!scan.read_u16(ncomp)) {
				return;
			}
			for (uint16_t j = 0; j < ncomp; ++j) {
				uint32_t t;
				if (!scan.read_u32(t)) {
					return;
				}
				const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
				if (!s) {
					return;
				}
				size_t sz;
				if (version == 1) {
					sz = vortaris::serialized_component_size(*s);
				} else {
					std::vector<bool> mask;
					if (!read_field_mask(scan, s->fields.size(), mask)) {
						return;
					}
					sz = vortaris::serialized_component_fields_size(*s, mask);
				}
				if (scan.pos() + sz > scan.size()) {
					return;
				}
				scan.seek(scan.pos() + sz);
			}
		}
	}
	// Pass 2: apply. Every read and every create is now guaranteed to succeed,
	// so the world transitions from the old state to the new one atomically.
	std::unordered_set<vortaris::Entity> incoming;
	for (uint32_t i = 0; i < n; ++i) {
		uint64_t id;
		if (!in.read_u64(id)) {
			return;
		}
		vortaris::Entity e{ id };
		if (w.is_alive(e)) {
			// Reconcile: the server snapshot is authoritative, so rebuild the
			// entity instead of aborting on an id conflict.
			w.destroy_entity(e);
		}
		e = w.create_entity_preassigned(id);
		if (!e) {
			return;
		}
		incoming.insert(e);
		uint16_t ncomp;
		if (!in.read_u16(ncomp)) {
			return;
		}
		for (uint16_t j = 0; j < ncomp; ++j) {
			uint32_t t;
			if (!in.read_u32(t)) {
				return;
			}
			const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
			if (!s) {
				return;
			}
			w.add_raw(e, t, nullptr);
			void *dst = w.get_raw(e, t);
			if (!dst) {
				return;
			}
			if (version == 1) {
				if (!vortaris::deserialize_component(*s, dst, in)) {
					return;
				}
			} else {
				std::vector<bool> mask;
				if (!read_field_mask(in, s->fields.size(), mask) ||
						!vortaris::deserialize_component_fields(*s, dst, in, mask)) {
					return;
				}
			}
		}
	}
	// Anti-ghost reconciliation: remove previously-replicated entities that are
	// no longer part of the server's snapshot.
	for (vortaris::Entity e : client_replicated_) {
		if (incoming.count(e) == 0 && w.is_alive(e)) {
			w.destroy_entity(e);
		}
	}
	client_replicated_ = std::move(incoming);
}

void VECSSnapshotReplication::apply_despawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint64_t id;
	if (!in.read_u64(id)) {
		return;
	}
	vortaris::Entity e{ id };
	client_replicated_.erase(e);
	if (w.is_alive(e)) {
		w.destroy_entity(e);
	}
}

void VECSSnapshotReplication::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_reconciliation_interval", "value"), &VECSSnapshotReplication::set_reconciliation_interval);
	ClassDB::bind_method(D_METHOD("get_reconciliation_interval"), &VECSSnapshotReplication::get_reconciliation_interval);
	ClassDB::add_property("VECSSnapshotReplication", PropertyInfo(Variant::FLOAT, "reconciliation_interval"), "set_reconciliation_interval", "get_reconciliation_interval");
}

// ------------------------------------------------------------ network sync --

VECSNetworkSync::VECSNetworkSync() {
	godot::Ref<VECSSnapshotReplication> sr;
	sr.instantiate();
	strategy_ = sr;

	godot::Dictionary cfg;
	cfg["rpc_mode"] = RPC_MODE_ANY_PEER;
	rpc_config("_rpc_spawn", cfg);
	rpc_config("_rpc_despawn", cfg);
	rpc_config("_rpc_delta", cfg);
	rpc_config("_rpc_full_state", cfg);
}

VECSNetworkSync::~VECSNetworkSync() {
}

void VECSNetworkSync::bind_world(VECSWorld *p_world) {
	if (world_ == p_world) {
		return;
	}
	if (world_ && observer_id_ > 0) {
		world_->core().observer_dispatch().remove(observer_id_);
		observer_id_ = 0;
	}
	world_ = p_world;
	if (world_) {
		vortaris::ObserverCallback cb;
		cb.event_mask = vortaris::EVENT_ADDED | vortaris::EVENT_CHANGED | vortaris::EVENT_REMOVED | vortaris::EVENT_CUSTOM;
		cb.watch_all = true;
		cb.fn = [this](vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
			_on_world_event(p_type, p_e, p_comp, p_name, p_payload);
		};
		observer_id_ = world_->core().observer_dispatch().add(std::move(cb));
		if (strategy_.is_valid()) {
			strategy_->seed_from_world(*this);
		}
		vortaris::log_debug("network sync bound to world (server=" + (server_ ? godot::String("yes") : godot::String("no")) + ")");
	}
}

void VECSNetworkSync::set_strategy(const godot::Ref<VECSSyncStrategy> &p_strategy) {
	strategy_ = p_strategy;
	if (strategy_.is_valid() && world_) {
		strategy_->seed_from_world(*this);
	}
}

void VECSNetworkSync::set_server(bool p_v) {
	server_ = p_v;
}

void VECSNetworkSync::tick(double p_delta) {
	if (strategy_.is_valid()) {
		strategy_->tick(*this, p_delta);
	}
}

void VECSNetworkSync::request_full_state() {
	if (strategy_.is_valid()) {
		strategy_->send_full_state(*this, 0);
	}
}

void VECSNetworkSync::send_packet(SyncPacketKind p_kind, const vortaris::BinaryBuffer &p_data, int64_t p_target_peer) {
	if (direct_peer_) {
		direct_peer_->apply_packet(p_kind, p_data);
		return;
	}
	PackedByteArray bytes = p_data.to_packed();
	switch (p_kind) {
		case SyncPacketKind::Spawn:
			rpc("_rpc_spawn", bytes, session_id_);
			break;
		case SyncPacketKind::Despawn:
			rpc("_rpc_despawn", bytes, session_id_);
			break;
		case SyncPacketKind::Delta:
			rpc("_rpc_delta", bytes, session_id_);
			break;
		case SyncPacketKind::FullState:
			if (p_target_peer > 0) {
				rpc_id(p_target_peer, "_rpc_full_state", bytes, session_id_);
			} else {
				rpc("_rpc_full_state", bytes, session_id_);
			}
			break;
	}
}

void VECSNetworkSync::apply_packet(SyncPacketKind p_kind, const vortaris::BinaryBuffer &p_data) {
	if (!world_ || strategy_.is_null()) {
		return;
	}
	// Drop malformed packets before any write: a truncated / schema-mismatched
	// packet must never leave a partially applied world behind.
	if (!validate_packet(p_data, p_kind)) {
		ERR_PRINT("VortarisECS: dropped a malformed network packet (kind " +
				godot::String::num_int64(static_cast<int64_t>(p_kind)) + ", " +
				godot::String::num_int64(static_cast<int64_t>(p_data.size())) + " bytes).");
		return;
	}
	applying_ = true;
	if (vortaris::verbose_active()) {
		vortaris::log_verbose("network apply packet kind=" + godot::String::num_int64(static_cast<int64_t>(p_kind)) +
				" bytes=" + godot::String::num_int64(static_cast<int64_t>(p_data.size())));
	}
	switch (p_kind) {
		case SyncPacketKind::Spawn:
			strategy_->apply_spawn(*this, p_data);
			break;
		case SyncPacketKind::Delta:
			strategy_->apply_delta(*this, p_data);
			break;
		case SyncPacketKind::FullState:
			strategy_->apply_full_state(*this, p_data);
			break;
		case SyncPacketKind::Despawn:
			strategy_->apply_despawn(*this, p_data);
			break;
	}
	applying_ = false;
}

void VECSNetworkSync::_rpc_spawn(const PackedByteArray &p_bytes, uint32_t p_session) {
	if (p_session != session_id_) {
		return;
	}
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_bytes);
	apply_packet(SyncPacketKind::Spawn, buf);
}

void VECSNetworkSync::_rpc_despawn(const PackedByteArray &p_bytes, uint32_t p_session) {
	if (p_session != session_id_) {
		return;
	}
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_bytes);
	apply_packet(SyncPacketKind::Despawn, buf);
}

void VECSNetworkSync::_rpc_delta(const PackedByteArray &p_bytes, uint32_t p_session) {
	if (p_session != session_id_) {
		return;
	}
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_bytes);
	apply_packet(SyncPacketKind::Delta, buf);
}

void VECSNetworkSync::_rpc_full_state(const PackedByteArray &p_bytes, uint32_t p_session) {
	if (p_session != session_id_) {
		return;
	}
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_bytes);
	apply_packet(SyncPacketKind::FullState, buf);
}

void VECSNetworkSync::reset() {
	if (world_ && observer_id_ > 0) {
		world_->core().observer_dispatch().remove(observer_id_);
		observer_id_ = 0;
	}
	if (strategy_.is_valid()) {
		strategy_->reset_state();
	}
}

void VECSNetworkSync::_on_world_event(vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
	if (strategy_.is_null()) {
		return;
	}
	switch (p_type) {
		case vortaris::ObserverEventType::Added:
			strategy_->on_entity_component_added(*this, p_e, p_comp);
			break;
		case vortaris::ObserverEventType::Changed:
			strategy_->on_entity_component_changed(*this, p_e, p_comp);
			break;
		case vortaris::ObserverEventType::Removed:
			strategy_->on_entity_component_removed(*this, p_e, p_comp);
			break;
		case vortaris::ObserverEventType::Custom:
			if (p_name == "world_cleared") {
				strategy_->reset_state();
			}
			break;
		default:
			break;
	}
}

void VECSNetworkSync::_notification(int p_what) {
	if (p_what == NOTIFICATION_PREDELETE && world_ && observer_id_ > 0) {
		world_->core().observer_dispatch().remove(observer_id_);
		observer_id_ = 0;
	}
}

void VECSNetworkSync::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bind_world", "world"), &VECSNetworkSync::bind_world);
	ClassDB::bind_method(D_METHOD("set_strategy", "strategy"), &VECSNetworkSync::set_strategy);
	ClassDB::bind_method(D_METHOD("get_strategy"), &VECSNetworkSync::get_strategy);
	ClassDB::bind_method(D_METHOD("set_server", "value"), &VECSNetworkSync::set_server);
	ClassDB::bind_method(D_METHOD("is_server"), &VECSNetworkSync::is_server);
	ClassDB::bind_method(D_METHOD("set_session_id", "value"), &VECSNetworkSync::set_session_id);
	ClassDB::bind_method(D_METHOD("get_session_id"), &VECSNetworkSync::get_session_id);
	ClassDB::bind_method(D_METHOD("tick", "delta"), &VECSNetworkSync::tick);
	ClassDB::bind_method(D_METHOD("request_full_state"), &VECSNetworkSync::request_full_state);
	ClassDB::bind_method(D_METHOD("set_direct_peer", "peer"), &VECSNetworkSync::set_direct_peer);
	ClassDB::bind_method(D_METHOD("get_direct_peer"), &VECSNetworkSync::get_direct_peer);
	ClassDB::bind_method(D_METHOD("reset"), &VECSNetworkSync::reset);
	ClassDB::bind_method(D_METHOD("_rpc_spawn", "bytes", "session"), &VECSNetworkSync::_rpc_spawn);
	ClassDB::bind_method(D_METHOD("_rpc_despawn", "bytes", "session"), &VECSNetworkSync::_rpc_despawn);
	ClassDB::bind_method(D_METHOD("_rpc_delta", "bytes", "session"), &VECSNetworkSync::_rpc_delta);
	ClassDB::bind_method(D_METHOD("_rpc_full_state", "bytes", "session"), &VECSNetworkSync::_rpc_full_state);
}
