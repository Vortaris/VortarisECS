extends VECSObserver

# Event-driven entry into the active set: when a sand block loses its support,
# the world emits "sand_support_broken"; this observer pushes the entity into
# the active set by adding the Falling marker. The system never has to scan
# every sand block every tick.

func _script_each(event: int, entity: VECSEntity, payload: Variant) -> void:
	if event == VECSObserver.Event.CUSTOM:
		entity.add_component("Falling", {"time": 0.0})
