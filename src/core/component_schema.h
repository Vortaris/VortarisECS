#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include <godot_cpp/variant/string_name.hpp>

#include "component_type.h"

namespace vortaris {

// The set of field types supported by the schema reflection system.
//
// Every type in this list is required to be trivially copyable so that
// component instances can be stored in contiguous SoA columns, memcpy'd
// during archetype transitions and serialized to a deterministic binary
// format without custom codecs.
enum class FieldType : uint8_t {
	Bool,
	I8, I16, I32, I64,
	U8, U16, U32, U64,
	F32, F64,
	Vector2, Vector2i, Vector3, Vector3i, Vector4, Vector4i,
	Color, Quaternion, Basis, Transform2D, Transform3D, AABB, Rect2, Plane,
	StringFixed, // fixed inline char buffer, byte length in `count`
	Blob,        // fixed raw byte buffer, byte length in `count`
};

// Network sync priority tiers. Mirrors GECS' layered dirty-checking.
enum SyncPriority : uint8_t {
	SYNC_REALTIME = 0, // every tick
	SYNC_HIGH = 1,     // 20 Hz
	SYNC_MEDIUM = 2,   // 10 Hz
	SYNC_LOW = 3,      // 2 Hz
	SYNC_SPAWN_ONLY = 4, // sent once inside the spawn packet
	SYNC_LOCAL = 5,      // never networked
};

struct FieldDescriptor {
	godot::StringName name;
	FieldType type = FieldType::Bool;
	size_t offset = 0;
	size_t byte_size = 0;
	size_t count = 1;        // fixed-array length; 1 => scalar
	uint8_t element_type = 0; // element FieldType for arrays
	uint8_t sync_priority = SYNC_MEDIUM;
	bool is_networked = true;

	// Total bytes the field occupies within the component (byte_size is the
	// size of one element, or of the whole buffer for StringFixed/Blob).
	size_t storage_size() const { return byte_size; }
	bool is_scalar() const { return type != FieldType::StringFixed && type != FieldType::Blob; }
};

struct ComponentSchema {
	godot::StringName type_name;
	ComponentTypeId type_id = INVALID_COMPONENT_TYPE;
	size_t size = 0;
	size_t alignment = 1;
	std::vector<FieldDescriptor> fields;
	bool is_networked = false;

	const FieldDescriptor *find_field(const godot::StringName &p_name) const {
		for (const auto &f : fields) {
			if (f.name == p_name) {
				return &f;
			}
		}
		return nullptr;
	}
};

} // namespace vortaris

namespace std {
template <>
struct hash<godot::StringName> {
	size_t operator()(const godot::StringName &p_name) const {
		return static_cast<size_t>(p_name.hash());
	}
};
} // namespace std
