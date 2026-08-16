extends EditorDebuggerPlugin
## VortarisECS runtime remote monitor — editor side.
##
## A [EditorDebuggerPlugin] registered by [method EditorPlugin.add_debugger_plugin]
## from `editor_plugin.gd`. While a game runs (F5), it adds an "ECS" tab to the
## editor's debugger bottom panel and renders world snapshots of the RUNNING
## game: entities, registered components, systems and world stats.
##
## Channel protocol (prefix [code]"vecs"[/code], see src/register_types.cpp for
## the game-side capture):
##   editor -> game:  "vecs:req_snapshot"  with data = []
##   game -> editor:  "vecs:snapshot"     with data = [ <snapshot Dictionary> ]
##
## The snapshot Dictionary comes from [method VECSWorld.get_snapshot_data]:
## { "protocol", "version", "stats", "components", "systems", "entities" }.

const MSG_REQ := "vecs:req_snapshot"
const MSG_SNAPSHOT := "vecs:snapshot"
const MSG_SET_FIELD := "vecs:set_field"
const MSG_SET_FIELD_RESULT := "vecs:set_field_result"
const PREFIX := "vecs"

const TabScene := preload("res://addons/vortarisecs/editor/ecs_debugger_tab.gd")

# session_id -> ecs_debugger_tab (kept so _capture can route messages).
var _tabs: Dictionary = {}


func _has_capture(capture: String) -> bool:
	return capture == PREFIX


func _setup_session(session_id: int) -> void:
	var session := get_session(session_id)
	if session == null:
		return
	# Replace a tab from a previous session that reused the same id.
	if _tabs.has(session_id) and is_instance_valid(_tabs[session_id]):
		(_tabs[session_id] as Control).queue_free()
	var tab: Control = TabScene.new()
	tab.plugin = self
	tab.session_id = session_id
	session.add_session_tab(tab)
	_tabs[session_id] = tab
	# A freshly created session may not be active yet (the game is still
	# connecting), so wait for the `started` signal before requesting data.
	# If it is already active (e.g. the plugin was re-registered after the game
	# connected) the `started` signal will never fire — start immediately.
	session.started.connect(_on_session_started.bind(session_id))
	session.stopped.connect(_on_session_stopped.bind(session_id))
	if session.is_active():
		_on_session_started(session_id)


func _on_session_started(session_id: int) -> void:
	var tab = _tabs.get(session_id)
	if is_instance_valid(tab):
		tab.set_connected(true)


func _on_session_stopped(session_id: int) -> void:
	var tab = _tabs.get(session_id)
	if is_instance_valid(tab):
		tab.set_connected(false)


func _capture(message: String, data: Array, session_id: int) -> bool:
	var tab = _tabs.get(session_id)
	if message == MSG_SNAPSHOT:
		if not is_instance_valid(tab):
			return true
		var snapshot: Dictionary = {}
		if data.size() > 0 and data[0] is Dictionary:
			snapshot = data[0]
		tab.set_snapshot(snapshot)
		return true
	if message == MSG_SET_FIELD_RESULT:
		if not is_instance_valid(tab):
			return true
		# data = [ok, entity_id, comp, field, error]
		tab.set_field_result(bool(data[0]) if data.size() > 0 else false,
				int(data[1]) if data.size() > 1 else 0,
				str(data[2]) if data.size() > 2 else "",
				str(data[3]) if data.size() > 3 else "",
				str(data[4]) if data.size() > 4 else "")
		return true
	return false


## Asks the running game for a fresh snapshot over the debugger channel.
func request_snapshot(session_id: int) -> void:
	var session := get_session(session_id)
	if session == null:
		return
	if not session.is_active():
		return
	session.send_message(MSG_REQ, [])


## Asks the running game to write a component field value over the debugger
## channel. Only valid while the session is active; the game replies on
## MSG_SET_FIELD_RESULT with the ok/error.
func send_set_field(session_id: int, entity_id: int, comp: String, field: String, value: Variant) -> void:
	var session := get_session(session_id)
	if session == null:
		return
	if not session.is_active():
		return
	session.send_message(MSG_SET_FIELD, [entity_id, comp, field, value])
