#pragma once

#include <cstdint>

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
	void _run(vortaris::World &p_world, double p_delta) override;

protected:
	static void _bind_methods();
};
