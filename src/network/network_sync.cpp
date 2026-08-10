#include "network_sync.h"

#include <algorithm>

#include <godot_cpp/variant/dictionary.hpp>

#include "../core/component_registry.h"
#include "../core/world.h"
#include "../serialization/component_serializer.h"
#include "gdscript/vecs_world.h"

using namespace godot;

namespace {
constexpr uint16_t SYNC_VERSION = 1;
constexpr int RPC_MODE_ANY_PEER = 2;

void patch_u32(vortaris::BinaryBuffer &p_buf, size_t p_pos, uint32_t p_value) {
	p_buf.overwrite_u32(p_pos, p_value);
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
	r_buf.write_u32(p_t);
	vortaris::serialize_component(*s, data, r_buf);
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
	for (const auto &kv : dirty_) {
		vortaris::Entity e = kv.first;
		if (!w.is_alive(e)) {
			continue;
		}
		vortaris::BinaryBuffer comp_buf;
		uint8_t ncomp = 0;
		for (vortaris::ComponentTypeId t : kv.second) {
			if (is_networked(t) && w.has(e, t)) {
				serialize_component_block(p_ns, e, t, comp_buf);
				++ncomp;
			}
		}
		if (ncomp == 0) {
			continue;
		}
		r_out.write_u64(e.id);
		r_out.write_u8(ncomp);
		r_out.write_bytes(comp_buf.data(), comp_buf.size());
		++n;
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
					const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
					comp_buf.write_u32(t);
					vortaris::serialize_component(*s, a->columns[i].row(row), comp_buf);
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
	vortaris::World &w = p_ns.world()->core();

	// Spawns (delayed one tick so multi-component entities spawn complete).
	if (!pending_spawn_.empty()) {
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
		}
		pending_spawn_.clear();
	}

	// Deltas (dirty networked components).
	if (!dirty_.empty()) {
		vortaris::BinaryBuffer buf;
		build_delta(p_ns, buf);
		if (buf.size() > 0) {
			p_ns.send_packet(SyncPacketKind::Delta, buf);
		}
		dirty_.clear();
	}

	// Despawns. The entity may already be dead locally — that is the point:
	// notify peers to remove their copy.
	if (!pending_despawn_.empty()) {
		for (vortaris::Entity e : pending_despawn_) {
			vortaris::BinaryBuffer buf;
			buf.write_u64(e.id);
			p_ns.send_packet(SyncPacketKind::Despawn, buf);
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
}

void VECSSnapshotReplication::apply_spawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint16_t version;
	if (!in.read_u16(version) || version != SYNC_VERSION) {
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
		if (!dst || !vortaris::deserialize_component(*s, dst, in)) {
			return;
		}
	}
	client_replicated_.insert(e);
}

void VECSSnapshotReplication::apply_delta(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {
	vortaris::World &w = p_ns.world()->core();
	vortaris::BinaryBuffer in = p_data;
	in.seek(0);
	uint16_t version;
	if (!in.read_u16(version) || version != SYNC_VERSION) {
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
			if (!w.is_alive(e)) {
				// The entity died locally (e.g. despawned while this delta was in
				// flight). We still must consume the component's bytes so the read
				// cursor stays aligned for the remaining entries; without this the
				// following reads would parse component payloads as type ids.
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
	if (!in.read_u16(version) || version != SYNC_VERSION) {
		return;
	}
	uint32_t n;
	if (!in.read_u32(n)) {
		return;
	}
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
			if (!dst || !vortaris::deserialize_component(*s, dst, in)) {
				return;
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
	applying_ = true;
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
