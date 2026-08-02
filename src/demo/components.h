#pragma once

#include "../src/reflect/component_macros.h"

// Example component types defined in C++ and registered at extension init.
// In a real game these would live in your own C++ plugin sources.

struct Position {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

struct Velocity {
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;
};

// Registers Position/Velocity with the global component registry. Called from
// the extension's SCENE-level initializer (same dll).
void vortaris_demo_register_components();
