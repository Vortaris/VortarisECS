extends VECSObserver

var event_log: Array = []

func _script_each(event: int, entity: VECSEntity, payload: Variant) -> void:
	event_log.append([event, entity.get_id()])
