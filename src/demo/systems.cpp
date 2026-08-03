#include "systems.h"

#include "core/world.h"

void MoveSystem::_tick(vortaris::World &p_world, double p_delta) {
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

void ViewSystem::_setup(vortaris::World &p_world) {
	view_ = p_world.view<Position, Velocity>();
	changes_ = p_world.changes<Position>();
}

void ViewSystem::_tick(vortaris::World &p_world, double p_delta) {
	view_count = 0;
	view_.each([&](vortaris::Entity e, Position &pos, Velocity &vel) {
		++view_count;
	});
	const auto changed = changes_.take();
	changed_count = static_cast<int64_t>(changed.size());
}

void ViewSystem::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("get_view_count"), &ViewSystem::get_view_count);
	ClassDB::bind_method(D_METHOD("get_changed_count"), &ViewSystem::get_changed_count);
}
