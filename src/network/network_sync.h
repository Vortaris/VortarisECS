#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "../core/component_type.h"
#include "../core/entity.h"
#include "../core/observer_dispatch.h"
#include "../serialization/binary_buffer.h"

class VECSWorld;

namespace vortaris {
class World;
}

// Packet kinds delivered between VECSNetworkSync peers.
enum class SyncPacketKind : uint8_t {
	Spawn = 0,
	Delta = 1,
	FullState = 2,
	Despawn = 3,
};

class VECSNetworkSync;

// Pluggable replication strategy. The default is VECSSnapshotReplication
// (server-authoritative dirty-checking + periodic reconciliation); swap in
// your own subclass (deterministic lockstep, event-only sync, ...) and the
// transport layer stays unchanged.
class VECSSyncStrategy : public godot::RefCounted {
	GDCLASS(VECSSyncStrategy, godot::RefCounted)

public:
	virtual void on_entity_component_added(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {}
	virtual void on_entity_component_changed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {}
	virtual void on_entity_component_removed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) {}
	virtual void tick(VECSNetworkSync &p_ns, double p_delta) {}
	virtual void apply_spawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {}
	virtual void apply_delta(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {}
	virtual void apply_full_state(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {}
	virtual void apply_despawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) {}
	virtual void on_peer_joined(VECSNetworkSync &p_ns, int64_t p_peer) { send_full_state(p_ns, p_peer); }
	virtual void send_full_state(VECSNetworkSync &p_ns, int64_t p_peer) {}
	virtual void seed_from_world(VECSNetworkSync &p_ns) {}
	virtual void reset_state() {}

protected:
	static void _bind_methods();
};

// Default server-authoritative snapshot replication.
class VECSSnapshotReplication : public VECSSyncStrategy {
	GDCLASS(VECSSnapshotReplication, VECSSyncStrategy)

public:
	void on_entity_component_added(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) override;
	void on_entity_component_changed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) override;
	void on_entity_component_removed(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t) override;
	void tick(VECSNetworkSync &p_ns, double p_delta) override;
	void apply_spawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) override;
	void apply_delta(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) override;
	void apply_full_state(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) override;
	void apply_despawn(VECSNetworkSync &p_ns, const vortaris::BinaryBuffer &p_data) override;
	void send_full_state(VECSNetworkSync &p_ns, int64_t p_peer) override;
	void reset_state() override;

	void set_reconciliation_interval(double p_v) { reconciliation_interval_ = p_v; }
	double get_reconciliation_interval() const { return reconciliation_interval_; }

	void seed_from_world(VECSNetworkSync &p_ns);

protected:
	static void _bind_methods();

private:
	static bool is_networked(vortaris::ComponentTypeId p_t);
	static void serialize_component_block(VECSNetworkSync &p_ns, vortaris::Entity p_e, vortaris::ComponentTypeId p_t, vortaris::BinaryBuffer &r_buf);
	void build_delta(VECSNetworkSync &p_ns, vortaris::BinaryBuffer &r_out);
	void serialize_full_state(VECSNetworkSync &p_ns, vortaris::BinaryBuffer &r_out);

	std::unordered_set<vortaris::Entity> tracked_;
	std::unordered_map<vortaris::Entity, std::unordered_set<vortaris::ComponentTypeId>> dirty_;
	std::unordered_set<vortaris::Entity> pending_spawn_;
	std::unordered_set<vortaris::Entity> pending_despawn_;
	std::unordered_set<vortaris::Entity> client_replicated_;
	int tick_count_ = 0;
	double reconcile_accum_ = 0.0;
	double reconciliation_interval_ = 30.0;
};

// Central network coordinator: binds one VECSWorld, owns a sync strategy and
// exposes the single RPC surface (spawn/despawn/delta/full_state). In real
// deployments the node must live at the SAME path in every peer's scene tree
// so the RPC routes correctly.
class VECSNetworkSync : public godot::Node {
	GDCLASS(VECSNetworkSync, godot::Node)

public:
	VECSNetworkSync();
	~VECSNetworkSync() override;

	void bind_world(VECSWorld *p_world);
	VECSWorld *world() { return world_; }

	void set_strategy(const godot::Ref<VECSSyncStrategy> &p_strategy);
	godot::Ref<VECSSyncStrategy> get_strategy() const { return strategy_; }

	void set_server(bool p_v);
	bool is_server() const { return server_; }
	void set_session_id(uint32_t p_v) { session_id_ = p_v; }
	uint32_t get_session_id() const { return session_id_; }
	void reset_session() { ++session_id_; }

	// Driven once per frame (call from a script's _process).
	void tick(double p_delta);
	// Sends a full state snapshot to every connected peer (reconciliation).
	void request_full_state();

	bool is_applying() const { return applying_; }

	// Delivers a serialized packet (RPC in real mode, direct in test mode).
	// p_target_peer > 0 delivers only to that peer (used for targeted full-state
	// reconciliation to a newly joined peer); 0 broadcasts to all peers.
	void send_packet(SyncPacketKind p_kind, const vortaris::BinaryBuffer &p_data, int64_t p_target_peer = 0);
	void apply_packet(SyncPacketKind p_kind, const vortaris::BinaryBuffer &p_data);

	// Test hook: bypass the engine RPC layer and deliver straight to a peer.
	void set_direct_peer(VECSNetworkSync *p_peer) { direct_peer_ = p_peer; }
	VECSNetworkSync *get_direct_peer() const { return direct_peer_; }

	// RPC surface (bound + rpc_config'd as ANY_PEER).
	void _rpc_spawn(const godot::PackedByteArray &p_bytes, uint32_t p_session);
	void _rpc_despawn(const godot::PackedByteArray &p_bytes, uint32_t p_session);
	void _rpc_delta(const godot::PackedByteArray &p_bytes, uint32_t p_session);
	void _rpc_full_state(const godot::PackedByteArray &p_bytes, uint32_t p_session);

	void reset();

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	void _on_world_event(vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload);

	VECSWorld *world_ = nullptr;
	vortaris::ObserverId observer_id_ = 0;
	godot::Ref<VECSSyncStrategy> strategy_;
	bool server_ = false;
	uint32_t session_id_ = 1;
	bool applying_ = false;
	VECSNetworkSync *direct_peer_ = nullptr;
};
