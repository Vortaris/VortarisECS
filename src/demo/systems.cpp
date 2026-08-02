#include "systems.h"

#include "core/world.h"

void MoveSystem::_run(vortaris::World &p_world, double p_delta) {
	processed_count = 0;
	const float dt = static_cast<float>(p_delta);
	p_world.for_each<Position, Velocity>([&](vortaris::Entity p_entity, Position &r_pos, Velocity &r_vel) {
		r_pos.x += r_vel.x * dt;
		r_pos.y += r_vel.y * dt;
		r_pos.z += r_vel.z * dt;
		++processed_count;
	});
}

void MoveSystem::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("get_processed_count"), &MoveSystem::get_processed_count);
}
