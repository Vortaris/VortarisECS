extends VECSSystem

# A system written entirely in GDScript: extends VECSSystem and overrides
# _script_process(delta). It drives the same world as C++ systems; here we
# integrate Position by Velocity through the script API (no C++ required).
#
# Register it like any system:
#   var s = preload("res://scripts/script_system.gd").new()
#   s.group = "scripts"
#   world.add_system(s)

func _script_process(delta: float) -> void:
	var world: VECSWorld = get_world_node()
	if world == null:
		return
	for e in world.query().with_all(["Position", "Velocity"]).execute():
		var pos: VECSComponent = e.get_component("Position")
		var vel: VECSComponent = e.get_component("Velocity")
		pos.set_field("x", pos.get_field("x") + vel.get_field("x") * delta)
		pos.set_field("y", pos.get_field("y") + vel.get_field("y") * delta)
		pos.set_field("z", pos.get_field("z") + vel.get_field("z") * delta)
