#include "command_buffer.h"

#include <cstring>

#include "world.h"

namespace vortaris {

void CommandBuffer::add_component(Entity p_e, ComponentTypeId p_t, const void *p_data, size_t p_size) {
	Command cmd;
	cmd.op = CmdOp::AddComponent;
	cmd.entity = p_e;
	cmd.type = p_t;
	if (p_data && p_size > 0) {
		cmd.data.assign(static_cast<const uint8_t *>(p_data), static_cast<const uint8_t *>(p_data) + p_size);
	}
	ops_.push_back(std::move(cmd));
}

void CommandBuffer::remove_component(Entity p_e, ComponentTypeId p_t) {
	Command cmd;
	cmd.op = CmdOp::RemoveComponent;
	cmd.entity = p_e;
	cmd.type = p_t;
	ops_.push_back(std::move(cmd));
}

void CommandBuffer::add_entity(Entity p_e) {
	Command cmd;
	cmd.op = CmdOp::AddEntity;
	cmd.entity = p_e;
	ops_.push_back(std::move(cmd));
}

void CommandBuffer::remove_entity(Entity p_e) {
	Command cmd;
	cmd.op = CmdOp::RemoveEntity;
	cmd.entity = p_e;
	ops_.push_back(std::move(cmd));
}

void CommandBuffer::add_custom(uint32_t p_id, const void *p_data, size_t p_size) {
	Command cmd;
	cmd.op = CmdOp::Custom;
	cmd.custom_id = p_id;
	if (p_data && p_size > 0) {
		cmd.data.assign(static_cast<const uint8_t *>(p_data), static_cast<const uint8_t *>(p_data) + p_size);
	}
	ops_.push_back(std::move(cmd));
}

void CommandBuffer::execute(World &p_world) {
	if (ops_.empty()) {
		return;
	}
	// Swap the queue out first so a flush triggered from inside a handler is
	// safe (re-entrancy).
	std::vector<Command> ops;
	ops.swap(ops_);

	p_world.begin_suppress();
	p_world.begin_deferred_moves();
	for (Command &cmd : ops) {
		switch (cmd.op) {
			case CmdOp::AddComponent:
				p_world.add_raw(cmd.entity, cmd.type, cmd.data.empty() ? nullptr : cmd.data.data());
				break;
			case CmdOp::RemoveComponent:
				p_world.remove_component(cmd.entity, cmd.type);
				break;
			case CmdOp::AddEntity:
				if (!p_world.is_alive(cmd.entity)) {
					p_world.create_entity_preassigned(cmd.entity.id);
				}
				break;
			case CmdOp::RemoveEntity:
				p_world.destroy_entity_deferred(cmd.entity);
				break;
			case CmdOp::Custom:
				p_world.run_custom_command(cmd.custom_id, cmd.data.empty() ? nullptr : cmd.data.data(), cmd.data.size());
				break;
		}
	}
	p_world.end_deferred_moves();
	p_world.end_suppress();
}

} // namespace vortaris
