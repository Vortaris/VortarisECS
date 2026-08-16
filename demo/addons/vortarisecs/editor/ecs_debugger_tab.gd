extends Control
## VortarisECS remote monitor session tab.
##
## The "ECS" tab added to the editor debugger panel by ecs_debugger_plugin.gd.
## Renders a snapshot Dictionary received from the RUNNING game into four pages:
##   Entities    — entity id -> component -> field = value (capped display)
##   Components  — registered component types (name, size, fields)
##   Systems     — registered systems (name, group, active/paused)
##   Stats       — get_debug_stats() numbers
##
## Data is fetched on demand (Refresh button) and optionally at ~1 Hz
## (Auto refresh). When no game is connected the tab shows a waiting hint.

var plugin: EditorDebuggerPlugin = null
var session_id: int = -1

const AUTO_REFRESH_INTERVAL := 1.0
const MAX_ENTITIES := 500

var _status_label: Label
var _auto_check: CheckBox
var _refresh_button: Button
var _tabs: TabContainer
var _entities_tree: Tree
var _components_tree: Tree
var _systems_tree: Tree
var _stats_tree: Tree
var _auto_timer: Timer
var _connected := false


func _ready() -> void:
	_build_ui()
	_auto_timer = Timer.new()
	_auto_timer.wait_time = AUTO_REFRESH_INTERVAL
	_auto_timer.timeout.connect(_on_auto_timeout)
	add_child(_auto_timer)
	_refresh_button.pressed.connect(_request_snapshot)
	_auto_check.toggled.connect(_on_auto_toggled)
	_show_waiting()
	set_connected(false)


func _exit_tree() -> void:
	if _auto_timer:
		_auto_timer.stop()
		_auto_timer.queue_free()
		_auto_timer = null


# ---------------------------------------------------------------- plugin API ----

func set_connected(connected: bool) -> void:
	_connected = connected
	if _auto_timer:
		_auto_timer.paused = not connected
	if connected:
		_status_label.text = "Connected (session %d) — VortarisECS" % session_id
		_status_label.add_theme_color_override("font_color", Color(0.45, 0.9, 0.45))
		_request_snapshot()
	else:
		_status_label.text = "Waiting for game (run with F5) — VortarisECS"
		_status_label.add_theme_color_override("font_color", Color(0.85, 0.85, 0.5))
		_status_label.tooltip_text = "Start the project from the editor (F5) to monitor its live ECS world."


func set_snapshot(snapshot: Dictionary) -> void:
	if _entities_tree == null:
		return
	_populate_stats(snapshot.get("stats", {}))
	_populate_components(snapshot.get("components", []))
	_populate_systems(snapshot.get("systems", []))
	_populate_entities(snapshot.get("entities", []))


# ---------------------------------------------------------------- UI build ----

func _build_ui() -> void:
	var root := VBoxContainer.new()
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_theme_constant_override("separation", 4)
	add_child(root)

	var toolbar := HBoxContainer.new()
	root.add_child(toolbar)

	_status_label = Label.new()
	toolbar.add_child(_status_label)

	var spacer := Control.new()
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(spacer)

	_auto_check = CheckBox.new()
	_auto_check.text = "Auto refresh (1s)"
	_auto_check.button_pressed = true
	_auto_check.tooltip_text = "Request a fresh snapshot about once per second."
	toolbar.add_child(_auto_check)

	_refresh_button = Button.new()
	_refresh_button.text = "Refresh"
	_refresh_button.tooltip_text = "Request one fresh snapshot from the running game."
	toolbar.add_child(_refresh_button)

	_tabs = TabContainer.new()
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(_tabs)

	_entities_tree = _make_tree(["Entity ID", "Component", "Field", "Value"], [220, 120, 140, 200])
	_tabs.add_child(_make_page("Entities", _entities_tree))

	_components_tree = _make_tree(["Component", "Field", "Type", "Count", "Sync", "Net"], [180, 120, 90, 60, 50, 50])
	_tabs.add_child(_make_page("Components", _components_tree))

	_systems_tree = _make_tree(["Name", "Group", "Active", "Paused", "Interval", "Flush"], [160, 110, 60, 60, 80, 60])
	_tabs.add_child(_make_page("Systems", _systems_tree))

	_stats_tree = _make_tree(["Stat", "Value"], [220, 160])
	_tabs.add_child(_make_page("Stats", _stats_tree))


func _make_tree(column_titles: Array, widths: Array) -> Tree:
	var tree := Tree.new()
	tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tree.column_titles_visible = true
	tree.hide_root = true
	tree.columns = column_titles.size()
	for i in column_titles.size():
		tree.set_column_title(i, column_titles[i])
		tree.set_column_expand(i, i == 0)
		tree.set_column_custom_minimum_width(i, widths[i] if i < widths.size() else 80)
	return tree


func _make_page(title: String, tree: Tree) -> Control:
	var page := MarginContainer.new()
	page.name = title
	page.add_theme_constant_override("margin_left", 4)
	page.add_theme_constant_override("margin_right", 4)
	page.add_theme_constant_override("margin_top", 4)
	page.add_theme_constant_override("margin_bottom", 4)
	page.add_child(tree)
	return page


# ---------------------------------------------------------------- population ----

func _populate_stats(stats: Variant) -> void:
	_stats_tree.clear()
	var root := _stats_tree.create_item()
	if not (stats is Dictionary):
		_placeholder_item(root, "no stats")
		return
	for key in (stats as Dictionary).keys():
		var item := _stats_tree.create_item(root)
		item.set_text(0, str(key))
		item.set_text(1, str((stats as Dictionary)[key]))


func _populate_components(components: Variant) -> void:
	_components_tree.clear()
	var root := _components_tree.create_item()
	if not (components is Array) or (components as Array).size() == 0:
		_placeholder_item(root, "no components registered")
		return
	for cdata in components:
		if not (cdata is Dictionary):
			continue
		var item := _components_tree.create_item(root)
		item.set_text(0, str(cdata.get("name", "?")))
		item.set_text(1, "size=%d" % int(cdata.get("size", 0)))
		var fields: Variant = cdata.get("fields", [])
		if fields is Array:
			for fd in fields:
				if not (fd is Dictionary):
					continue
				var fitem := _components_tree.create_item(item)
				fitem.set_text(1, str(fd.get("name", "")))
				fitem.set_text(2, str(fd.get("type", "")))
				fitem.set_text(3, str(fd.get("count", 1)))
				fitem.set_text(4, str(fd.get("sync_priority", "")))
				fitem.set_text(5, str(fd.get("networked", "")))


func _populate_systems(systems: Variant) -> void:
	_systems_tree.clear()
	var root := _systems_tree.create_item()
	if not (systems is Array) or (systems as Array).size() == 0:
		_placeholder_item(root, "no systems registered")
		return
	for sdata in systems:
		if not (sdata is Dictionary):
			continue
		var item := _systems_tree.create_item(root)
		item.set_text(0, str(sdata.get("name", "?")))
		item.set_text(1, str(sdata.get("group", "")))
		item.set_text(2, "true" if bool(sdata.get("active", true)) else "false")
		item.set_text(3, "true" if bool(sdata.get("paused", false)) else "false")
		item.set_text(4, str(sdata.get("tick_interval", 0.0)))
		item.set_text(5, str(sdata.get("flush_mode", 0)))


func _populate_entities(entities: Variant) -> void:
	_entities_tree.clear()
	var root := _entities_tree.create_item()
	if not (entities is Array) or (entities as Array).size() == 0:
		_placeholder_item(root, "no entities")
		return
	var count := 0
	var total: int = (entities as Array).size()
	for edata in entities:
		if count >= MAX_ENTITIES:
			var note := _entities_tree.create_item(root)
			note.set_text(0, "… %d more entities not shown (limit %d)" % [total - count, MAX_ENTITIES])
			break
		if not (edata is Dictionary):
			continue
		count += 1
		var eitem := _entities_tree.create_item(root)
		eitem.set_text(0, "Entity #%d" % int(edata.get("id", 0)))
		var comps: Variant = edata.get("components", {})
		if not (comps is Dictionary):
			continue
		for cname in comps:
			var citem := _entities_tree.create_item(eitem)
			citem.set_text(1, str(cname))
			var fields: Variant = (comps as Dictionary)[cname]
			if fields is Dictionary:
				for fname in fields:
					var fitem := _entities_tree.create_item(citem)
					fitem.set_text(2, str(fname))
					fitem.set_text(3, str((fields as Dictionary)[fname]))


func _placeholder_item(root: TreeItem, text: String) -> void:
	var item := root.get_tree().create_item(root)
	item.set_text(0, text)


func _show_waiting() -> void:
	_entities_tree.clear()
	_placeholder_item(_entities_tree.create_item(), "Waiting for the game…")
	_components_tree.clear()
	_placeholder_item(_components_tree.create_item(), "—")
	_systems_tree.clear()
	_placeholder_item(_systems_tree.create_item(), "—")
	_stats_tree.clear()
	_placeholder_item(_stats_tree.create_item(), "—")


# ---------------------------------------------------------------- requests ----

func _request_snapshot() -> void:
	if not _connected or plugin == null:
		return
	plugin.request_snapshot(session_id)


func _on_auto_timeout() -> void:
	if _auto_check.button_pressed:
		_request_snapshot()


func _on_auto_toggled(_pressed: bool) -> void:
	_request_snapshot()
