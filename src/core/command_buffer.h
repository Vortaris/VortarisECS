#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "component_type.h"
#include "entity.h"

namespace vortaris {

class World;

enum class CmdOp : uint8_t {
	AddComponent,
	RemoveComponent,
	AddEntity,
	RemoveEntity,
	Custom,
};

// A single queued structural change. Component data (for AddComponent) and
// custom payloads are copied inline so the buffer stays self-contained.
struct Command {
	CmdOp op = CmdOp::AddComponent;
	Entity entity;
	ComponentTypeId type = 0;
	std::vector<uint8_t> data;
	uint32_t custom_id = 0;
};

// Deferred, batched structural changes. Ops are queued as a flat array (no
// lambdas) and executed against the World inside a suppression + deferred-move
// window so that multiple changes to one entity collapse into a single
// archetype transition and a single cache invalidation.
class CommandBuffer {
public:
	void add_component(Entity p_e, ComponentTypeId p_t, const void *p_data, size_t p_size);
	void remove_component(Entity p_e, ComponentTypeId p_t);
	void add_entity(Entity p_e);
	void remove_entity(Entity p_e);
	void add_custom(uint32_t p_id, const void *p_data, size_t p_size);

	bool is_empty() const { return ops_.empty(); }
	size_t size() const { return ops_.size(); }
	void clear() { ops_.clear(); }

	void execute(World &p_world);

private:
	std::vector<Command> ops_;
};

} // namespace vortaris
