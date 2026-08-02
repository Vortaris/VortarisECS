#include "components.h"

void vortaris_demo_register_components() {
	VECS_REGISTER_COMPONENT(Position,
			VECS_FIELD(Position, x, F32),
			VECS_FIELD(Position, y, F32),
			VECS_FIELD(Position, z, F32));
	VECS_REGISTER_COMPONENT(Velocity,
			VECS_FIELD(Velocity, x, F32),
			VECS_FIELD(Velocity, y, F32),
			VECS_FIELD(Velocity, z, F32));
}
