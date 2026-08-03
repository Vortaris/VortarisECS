#pragma once

#include <cstdint>

#include "core/world.h"
#include "gdscript/vecs_system.h"

#include "components.h"

// Example C++ system: integrates Position by Velocity. Demonstrates the typed
// World::for_each<Position, Velocity> hot path.
class MoveSystem : public VECSSystem {
	GDCLASS(MoveSystem, VECSSystem)

public:
	int64_t processed_count = 0;
	int64_t get_processed_count() const { return processed_count; }

	void _setup(vortaris::World &p_world) override {}
	void _tick(vortaris::World &p_world, double p_delta) override;

protected:
	static void _bind_methods();
};

// Demonstrates the "cached data contract" interfaces: View (compile the query
// once in _setup, reuse each tick) and ChangeView (only entities whose watched
// component changed since the previous take()).
class ViewSystem : public VECSSystem {
	GDCLASS(ViewSystem, VECSSystem)

public:
	int64_t view_count = 0;
	int64_t changed_count = 0;
	int64_t get_view_count() const { return view_count; }
	int64_t get_changed_count() const { return changed_count; }

	void _setup(vortaris::World &p_world) override;
	void _tick(vortaris::World &p_world, double p_delta) override;

protected:
	static void _bind_methods();

private:
	vortaris::View<Position, Velocity> view_;
	vortaris::ChangeView<Position> changes_;
};
