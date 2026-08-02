#pragma once

#include <memory>

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "../core/world.h"

#include "vecs_scheduler.h"

class VECSEntity;
class VECSComponentType;
class VECSQueryBuilder;
class VECSCommandBuffer;
class VECSSystem;
class VECSObserver;

// A scene/singleton node wrapping one vortaris::World. This is the main
// GDScript-facing entry point; it is registered as the "VECS" engine
// singleton, so scripts use `VECS.create_entity()` etc. directly.
class VECSWorld : public godot::Node {
	GDCLASS(VECSWorld, godot::Node)

public:
	VECSWorld();
	~VECSWorld() override = default;

	vortaris::World &core() { return *core_; }
	const vortaris::World &core() const { return *core_; }

	// ---- entity ----
	godot::Ref<VECSEntity> create_entity();
	godot::Ref<VECSEntity> create_entity_preassigned(int64_t p_id);
	void destroy_entity(const godot::Ref<VECSEntity> &p_entity);
	bool is_alive(const godot::Ref<VECSEntity> &p_entity) const;
	int64_t entity_count() const;
	void set_entity_range(int64_t p_base);

	// ---- component types ----
	godot::Ref<VECSComponentType> get_component_type(const godot::String &p_name);

	// ---- query / commands ----
	godot::Ref<VECSQueryBuilder> query();
	godot::Ref<VECSCommandBuffer> commands();

	// ---- systems ----
	void add_system(VECSSystem *p_system);
	void remove_system(VECSSystem *p_system);
	int64_t system_count() const;

	// ---- observers / events ----
	void add_observer(VECSObserver *p_observer);
	void remove_observer(VECSObserver *p_observer);
	void emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload);

	// ---- per-frame driver ----
	void process(double p_delta, const godot::String &p_group);

	void compact();
	VECSWorld *get_world();

	// ---- snapshot serialization ----
	godot::PackedByteArray serialize_snapshot() const;
	bool deserialize_snapshot(const godot::PackedByteArray &p_data);

protected:
	static void _bind_methods();

private:
	std::unique_ptr<vortaris::World> core_;
	std::unique_ptr<vortaris::SystemScheduler> scheduler_;
};
