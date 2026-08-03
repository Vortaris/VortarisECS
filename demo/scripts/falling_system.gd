extends VECSSystem

# Active-set system: only entities carrying the "Falling" marker are processed.
# Entities enter the active set via an event (see sand_observer.gd) and leave it
# when they settle (the marker is removed). The query builder is cached once and
# reused every tick — nothing is rescanned, only the active rows are handled.

var q: VECSQueryBuilder = null

func _script_process(delta: float) -> void:
	var world: VECSWorld = get_world_node()
	if world == null:
		return
	if q == null:
		q = world.query().with_all(["Falling"])
	for e in q.execute():
		var pos: VECSComponent = e.get_component("Position")
		var f: VECSComponent = e.get_component("Falling")
		pos.set_field("y", pos.get_field("y") - 5.0 * delta)
		f.set_field("time", f.get_field("time") + delta)
		if f.get_field("time") > 2.0:
			e.remove_component("Falling")   # settled: leave the active set
