#pragma once

#include <cstddef>

#include "../core/component_registry.h"
#include "../core/component_schema.h"

// Declares one reflectable field of a component struct.
//
// FIELDTYPE must be a bare FieldType enumerator name (Bool, I32, F32,
// Vector3, Transform3D, ...). The field is networked by default with medium
// priority; use VECS_FIELD_EX for finer control.
#define VECS_FIELD(T, member, FIELDTYPE) \
	::vortaris::FieldDescriptor { \
		::godot::StringName(#member), \
		::vortaris::FieldType::FIELDTYPE, \
		offsetof(T, member), \
		sizeof(((T *)nullptr)->member), \
		1, 0, \
		::vortaris::SYNC_MEDIUM, \
		true \
	}

// Like VECS_FIELD but with explicit sync priority and network flag.
#define VECS_FIELD_EX(T, member, FIELDTYPE, SYNC_PRIORITY, IS_NETWORKED) \
	::vortaris::FieldDescriptor { \
		::godot::StringName(#member), \
		::vortaris::FieldType::FIELDTYPE, \
		offsetof(T, member), \
		sizeof(((T *)nullptr)->member), \
		1, 0, \
		::vortaris::SYNC_PRIORITY, \
		IS_NETWORKED \
	}

// Registers a component struct with the given list of VECS_FIELD(...)
// descriptors. Returns the assigned ComponentTypeId. Registration happens at
// runtime (typically from the extension's SCENE-level initializer).
#define VECS_REGISTER_COMPONENT(T, ...) \
	(::vortaris::register_component_type<T>(::godot::StringName(#T), { __VA_ARGS__ }))
