#pragma once

#include <cstdint>
#include <functional>

namespace vortaris {

// A lightweight 64-bit entity handle.
//
// Layout:
//   bits 0..31 : slot index + 1   (0 => "not allocated" sentinel)
//   bits 32..63: generation
//
// The generation is bumped every time a slot is recycled, so a stale handle
// can be rejected in O(1) via World::is_alive().
struct Entity {
	uint64_t id = 0;

	uint32_t slot() const { return static_cast<uint32_t>(id & 0xFFFFFFFFu) - 1; }
	uint32_t generation() const { return static_cast<uint32_t>(id >> 32); }

	bool operator==(const Entity &p_other) const { return id == p_other.id; }
	bool operator!=(const Entity &p_other) const { return id != p_other.id; }
	explicit operator bool() const { return id != 0; }
};

} // namespace vortaris

namespace std {
template <>
struct hash<vortaris::Entity> {
	size_t operator()(const vortaris::Entity &p_entity) const {
		return std::hash<uint64_t>{}(p_entity.id);
	}
};
} // namespace std
