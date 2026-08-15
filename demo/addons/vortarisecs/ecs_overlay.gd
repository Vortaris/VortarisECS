extends CanvasLayer
## VortarisECS runtime debug overlay.
##
## An in-game (process-local) debug HUD for browsing the LIVE ECS world that the
## running game actually uses. Unlike the editor inspector dock
## (editor/ecs_inspector_dock.gd), which lives in the EDITOR process and can
## only see the editor-side (empty) world because of process isolation, this
## overlay runs inside the game process and sees the real world.
##
## Features
##   - Stats: entity_count / archetype_count / component_count / observer_count /
##     change_tick and a sampled query execution time (get_debug_stats() +
##     VECSQueryBuilder.get_last_execution_time_usec()).
##   - Browser: "Refresh" lists every entity -> component -> field as an
##     expandable tree (world.entities_to_data()).
##   - Snapshot: export serialize_snapshot_json() to user://vortarisecs_snapshot.json
##     and import it back (deserialize_snapshot_json).
##
## Off by default. Enable with the startup argument (parsed by the demo bootstrap):
##   godot --path demo -- --vortaris-ecs-overlay on
## or call set_overlay_enabled(true) at runtime. While the game runs, press F2 to
## toggle the overlay.

const SNAPSHOT_PATH := "user://vortarisecs_snapshot.json"
const TOGGLE_KEY := Key.KEY_F2

var _world: VECSWorld = null
var _enabled := false
var _refresh_timer := 0.0

var _panel: PanelContainer = null
var _stats_label: Label = null
var _entity_tree: Tree = null
var _status_label: Label = null
var _refresh_button: Button = null
var _export_button: Button = null
var _import_button: Button = null


func _ready() -> void:
	layer = 100
	_build_ui()
	_resolve_world()
	# Default off; main.gd / callers enable it explicitly.
	set_overlay_enabled(false)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode == TOGGLE_KEY:
		set_overlay_enabled(not _enabled)
		get_viewport().set_input_as_handled()


func _process(delta: float) -> void:
	if not _enabled:
		return
	_refresh_timer -= delta
	if _refresh_timer <= 0.0:
		_refresh_timer = 0.25
		_refresh_stats()


func set_overlay_enabled(enabled: bool) -> void:
	_enabled = enabled
	if _panel:
		_panel.visible = enabled
		if enabled:
			_refresh_stats()
			_refresh_browser()


func is_overlay_enabled() -> bool:
	return _enabled


# ---------------------------------------------------------------- UI ----

func _build_ui() -> void:
	_panel = PanelContainer.new()
	_panel.name = "VortarisECSOverlayPanel"
	_panel.set_anchors_preset(Control.PRESET_TOP_LEFT)
	_panel.position = Vector2(8, 8)
	add_child(_panel)

	var margin := MarginContainer.new()
	margin.add_theme_constant_override("margin_left", 10)
	margin.add_theme_constant_override("margin_right", 10)
	margin.add_theme_constant_override("margin_top", 8)
	margin.add_theme_constant_override("margin_bottom", 8)
	_panel.add_child(margin)

	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	margin.add_child(box)

	var title := Label.new()
	title.text = "VortarisECS Runtime Overlay  [F2 to toggle]"
	title.add_theme_font_size_override("font_size", 16)
	box.add_child(title)

	_stats_label = Label.new()
	_stats_label.name = "StatsLabel"
	_stats_label.text = "world unavailable"
	box.add_child(_stats_label)

	_entity_tree = Tree.new()
	_entity_tree.name = "EntityTree"
	_entity_tree.custom_minimum_size = Vector2(460, 220)
	_entity_tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	_entity_tree.columns = 1
	box.add_child(_entity_tree)

	var buttons := HBoxContainer.new()
	buttons.add_theme_constant_override("separation", 6)
	box.add_child(buttons)

	_refresh_button = _make_button("Refresh", _refresh_browser)
	buttons.add_child(_refresh_button)

	_export_button = _make_button("Export Snapshot", _export_snapshot)
	buttons.add_child(_export_button)

	_import_button = _make_button("Import Snapshot", _import_snapshot)
	buttons.add_child(_import_button)

	var close := _make_button("Close", func() -> void: set_overlay_enabled(false))
	buttons.add_child(close)

	_status_label = Label.new()
	_status_label.name = "StatusLabel"
	_status_label.text = ""
	box.add_child(_status_label)


func _make_button(text: String, on_pressed: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.pressed.connect(on_pressed)
	return b


# ------------------------------------------------------------- logic ----

func _resolve_world() -> void:
	if Engine.has_singleton("VECS"):
		_world = Engine.get_singleton("VECS") as VECSWorld
	else:
		_world = null


func _refresh_stats() -> void:
	_resolve_world()
	if _world == null:
		_stats_label.text = "VECS world unavailable"
		return
	var stats: Dictionary = _world.get_debug_stats()
	# Sample a broad query for the timing line (empty with_all matches all archetypes).
	var q := _world.query().with_all([])
	q.execute()
	_stats_label.text = (
		"entities=%d   archetypes=%d   components=%d   observers=%d\n"
		+ "change_tick=%d   pool=%d   query_cache=%d\n"
		+ "last query exec: %d usec"
	) % [
		stats.get("entity_count", 0),
		stats.get("archetype_count", 0),
		stats.get("component_count", 0),
		stats.get("observer_count", 0),
		stats.get("change_tick", 0),
		stats.get("pool_size", 0),
		stats.get("query_cache_entries", 0),
		q.get_last_execution_time_usec(),
	]


func _refresh_browser() -> void:
	_resolve_world()
	_entity_tree.clear()
	if _world == null:
		_set_status("VECS world unavailable")
		return
	var root: TreeItem = _entity_tree.create_item()
	root.set_text(0, "Entities (%d)" % _world.entity_count())
	var data: Array = _world.entities_to_data()
	for edata in data:
		var item := _entity_tree.create_item(root)
		item.set_text(0, "id=%d" % int(edata.get("id", 0)))
		var comps: Dictionary = edata.get("components", {})
		for cname in comps:
			var comp_item := _entity_tree.create_item(item)
			comp_item.set_text(0, cname)
			var fields: Dictionary = comps[cname]
			for fname in fields:
				var fitem := _entity_tree.create_item(comp_item)
				fitem.set_text(0, "%s = %s" % [fname, str(fields[fname])])
		item.collapsed = true  # keep entities collapsed until expanded


func _export_snapshot() -> void:
	_resolve_world()
	if _world == null:
		return
	var save: Dictionary = _world.serialize_snapshot_json()
	var text: String = JSON.stringify(save, "\t")
	var f := FileAccess.open(SNAPSHOT_PATH, FileAccess.WRITE)
	if f == null:
		_set_status("export failed: cannot open " + SNAPSHOT_PATH)
		return
	f.store_string(text)
	f.close()
	_set_status("snapshot exported to %s (%d bytes)" % [SNAPSHOT_PATH, text.length()])


func _import_snapshot() -> void:
	_resolve_world()
	if _world == null:
		return
	if not FileAccess.file_exists(SNAPSHOT_PATH):
		_set_status("import failed: no snapshot at " + SNAPSHOT_PATH)
		return
	var text: String = FileAccess.get_file_as_string(SNAPSHOT_PATH)
	var ok: bool = _world.deserialize_snapshot_json(text)
	_set_status("snapshot imported: %s (entities=%d)" % [str(ok), _world.entity_count()])
	_refresh_stats()
	_refresh_browser()


func _set_status(msg: String) -> void:
	if _status_label:
		_status_label.text = msg
