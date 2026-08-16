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
## Data is fetched on demand (Refresh button) and optionally at the interval
## from `vortarisecs/debug/auto_refresh_interval` (default ~1 Hz, Auto refresh).
## When no game is connected the tab shows a waiting hint. The Entities page
## caps the rendered rows at `vortarisecs/general/max_snapshot_entities`.

var plugin: EditorDebuggerPlugin = null
var session_id: int = -1

const DEFAULT_AUTO_REFRESH_INTERVAL := 1.0
const DEFAULT_MAX_ENTITIES := 500

var _auto_refresh_interval: float = DEFAULT_AUTO_REFRESH_INTERVAL
var _max_entities: int = DEFAULT_MAX_ENTITIES

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
	# The session tab is inserted straight into the editor debugger's
	# TabContainer with no scene file, so the root Control must be named
	# explicitly or it shows up as "@control<id>".
	name = "ECS"
	# Read the tunable settings fresh each time the tab is (re)built so a change
	# in Project Settings takes effect on the next editor session.
	_auto_refresh_interval = float(ProjectSettings.get_setting(
			"vortarisecs/debug/auto_refresh_interval", DEFAULT_AUTO_REFRESH_INTERVAL))
	_max_entities = int(ProjectSettings.get_setting(
			"vortarisecs/general/max_snapshot_entities", DEFAULT_MAX_ENTITIES))
	_build_ui()
	_auto_timer = Timer.new()
	_auto_timer.name = "AutoRefreshTimer"
	_auto_timer.wait_time = _auto_refresh_interval
	_auto_timer.timeout.connect(_on_auto_timeout)
	add_child(_auto_timer)
	# A freshly created Timer is NOT running: start() is what arms the
	# auto-refresh loop (without it only the manual Refresh button worked).
	# set_connected() then pauses/resumes it so it only fires while a game lives.
	_auto_timer.start()
	_refresh_button.pressed.connect(_request_snapshot)
	_auto_check.toggled.connect(_on_auto_toggled)
	# Live value editing: the Entities page's Value cells are editable and each
	# edit is sent to the RUNNING game over the debugger channel (E8).
	_entities_tree.item_edited.connect(_on_entity_field_edited)
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
	root.name = "Root"
	root.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	root.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_theme_constant_override("separation", 4)
	add_child(root)

	var toolbar := HBoxContainer.new()
	toolbar.name = "Toolbar"
	root.add_child(toolbar)

	_status_label = Label.new()
	_status_label.name = "StatusLabel"
	toolbar.add_child(_status_label)

	# Vertical rule between the status readout and the action controls.
	var toolbar_sep := VSeparator.new()
	toolbar_sep.name = "ToolbarSeparator"
	toolbar.add_child(toolbar_sep)

	var spacer := Control.new()
	spacer.name = "Spacer"
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	toolbar.add_child(spacer)

	_auto_check = CheckBox.new()
	_auto_check.name = "AutoRefreshCheck"
	# NOTE: Godot's String % supports %s/%d/%f/%v (and a few more), but NOT %g;
	# using %g throws "String formatting error: unsupported format character".
	_auto_check.text = "Auto refresh (%.1fs)" % _auto_refresh_interval
	_auto_check.button_pressed = true
	_auto_check.tooltip_text = "Request a fresh snapshot every %.1f seconds." % _auto_refresh_interval
	toolbar.add_child(_auto_check)

	_refresh_button = Button.new()
	_refresh_button.name = "RefreshButton"
	_refresh_button.text = "Refresh"
	_refresh_button.tooltip_text = "Request one fresh snapshot from the running game."
	toolbar.add_child(_refresh_button)

	# Horizontal rule separating the toolbar from the data tables below.
	var toolbar_tabs_sep := HSeparator.new()
	toolbar_tabs_sep.name = "ToolbarTabsSeparator"
	root.add_child(toolbar_tabs_sep)

	_tabs = TabContainer.new()
	_tabs.name = "Tabs"
	_tabs.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(_tabs)

	_entities_tree = _make_tree(["Entity ID", "Component", "Field", "Value"], [220, 120, 140, 200])
	_entities_tree.name = "EntitiesTree"
	_tabs.add_child(_make_page("Entities", _entities_tree))

	_components_tree = _make_tree(["Component", "Field", "Type", "Count", "Sync", "Net"], [180, 120, 90, 60, 50, 50])
	_components_tree.name = "ComponentsTree"
	_tabs.add_child(_make_page("Components", _components_tree))

	_systems_tree = _make_tree(["Name", "Group", "Active", "Paused", "Interval", "Flush"], [160, 110, 60, 60, 80, 60])
	_systems_tree.name = "SystemsTree"
	_tabs.add_child(_make_page("Systems", _systems_tree))

	_stats_tree = _make_tree(["Stat", "Value"], [220, 160])
	_stats_tree.name = "StatsTree"
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
	var page := VBoxContainer.new()
	page.name = title
	page.add_theme_constant_override("separation", 4)
	# Horizontal rule under the tab bar so each page reads as
	# separator / table (matches the toolbar separator above the tabs).
	var sep := HSeparator.new()
	sep.name = "PageSeparator"
	page.add_child(sep)
	var margin := MarginContainer.new()
	margin.name = "TreeMargin"
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	margin.add_theme_constant_override("margin_left", 4)
	margin.add_theme_constant_override("margin_right", 4)
	margin.add_theme_constant_override("margin_top", 4)
	margin.add_theme_constant_override("margin_bottom", 4)
	margin.add_child(tree)
	page.add_child(margin)
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
		if count >= _max_entities:
			var note := _entities_tree.create_item(root)
			note.set_text(0, "… %d more entities not shown (limit %d)" % [total - count, _max_entities])
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
					var raw_value: Variant = (fields as Dictionary)[fname]
					fitem.set_text(3, str(raw_value))
					# Runtime-debug editing (E8): the Value cell is editable and
					# carries the original Variant + addressing metadata, so the
					# edited text can be coerced back to the field's real type
					# before it is sent to the running game.
					fitem.set_editable(3, true)
					fitem.set_metadata(0, {
						"eid": int(edata.get("id", 0)),
						"comp": str(cname),
						"field": str(fname),
						"value": raw_value,
					})
					# Field VALUES default collapsed: the page then reads as
					# entity -> component -> (folded fields), which stays
					# navigable even with hundreds of entities.
					fitem.collapsed = true


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


# ---------------------------------------------------------------- live editing ----

func _on_entity_field_edited() -> void:
	var item := _entities_tree.get_edited()
	var col := _entities_tree.get_edited_column()
	if item == null or col != 3:
		return
	var meta: Dictionary = item.get_metadata(0)
	if not meta.has("eid"):
		return
	var eid: int = int(meta.get("eid", 0))
	var comp: String = str(meta.get("comp", ""))
	var field: String = str(meta.get("field", ""))
	var original: Variant = meta.get("value")
	var value := _coerce_field_value(original, item.get_text(col))
	if value == original:
		return  # unparsable / unchanged — keep the last known good text
	_send_set_field(eid, comp, field, value)


func _send_set_field(entity_id: int, comp: String, field: String, value: Variant) -> void:
	if not _connected or plugin == null:
		return
	plugin.send_set_field(session_id, entity_id, comp, field, value)
	_status_label.text = "Set %s.%s on #%d — sent…" % [comp, field, entity_id]
	_status_label.add_theme_color_override("font_color", Color(0.9, 0.85, 0.5))


func set_field_result(ok: bool, entity_id: int, comp: String, field: String, error: String) -> void:
	if ok:
		_status_label.text = "Set %s.%s on #%d — applied" % [comp, field, entity_id]
		_status_label.add_theme_color_override("font_color", Color(0.45, 0.9, 0.45))
		_request_snapshot()
	else:
		_status_label.text = "Set %s.%s on #%d failed: %s" % [comp, field, entity_id, error]
		_status_label.add_theme_color_override("font_color", Color(0.9, 0.5, 0.4))


## Parses the text a user typed into a Value cell back into the Variant type of
## the original value. Returns the original value when the text cannot be parsed,
## so a bad edit degrades to "no change" instead of corrupting the field.
func _coerce_field_value(original: Variant, text: String) -> Variant:
	match typeof(original):
		TYPE_FLOAT:
			return float(text) if text.is_valid_float() else original
		TYPE_INT:
			return int(text) if text.is_valid_int() else original
		TYPE_BOOL:
			var s := text.strip_edges().to_lower()
			return s == "true" or s == "1" or s == "yes" or s == "on"
		TYPE_STRING:
			return text
		TYPE_VECTOR2:
			var p := _split_numbers(text, 2)
			return Vector2(p[0], p[1]) if p.size() == 2 else original
		TYPE_VECTOR2I:
			var p := _split_ints(text, 2)
			return Vector2i(p[0], p[1]) if p.size() == 2 else original
		TYPE_VECTOR3:
			var p := _split_numbers(text, 3)
			return Vector3(p[0], p[1], p[2]) if p.size() == 3 else original
		TYPE_VECTOR3I:
			var p := _split_ints(text, 3)
			return Vector3i(p[0], p[1], p[2]) if p.size() == 3 else original
		TYPE_VECTOR4:
			var p := _split_numbers(text, 4)
			return Vector4(p[0], p[1], p[2], p[3]) if p.size() == 4 else original
		TYPE_VECTOR4I:
			var p := _split_ints(text, 4)
			return Vector4i(p[0], p[1], p[2], p[3]) if p.size() == 4 else original
		TYPE_COLOR:
			var p := _split_numbers(text, 4, true)
			if p.size() == 3:
				return Color(p[0], p[1], p[2])
			if p.size() == 4:
				return Color(p[0], p[1], p[2], p[3])
			return original
		TYPE_QUATERNION:
			var p := _split_numbers(text, 4)
			return Quaternion(p[0], p[1], p[2], p[3]) if p.size() == 4 else original
		_:
			# Best-effort for the remaining Godot types (Basis, Transform2D/3D,
			# Rect2, AABB, Plane...): accept Godot's variant-literal syntax,
			# e.g. "Vector3(1, 2, 3)" or "Transform3D(...)".
			var v := str_to_var(text)
			return v if v != null else original
	return original


func _split_numbers(text: String, count: int, allow_short: bool = false) -> PackedFloat64Array:
	var clean := text.replace("(", "").replace(")", "").replace("[", "").replace("]", "")
	var out := PackedFloat64Array()
	for part in clean.split(",", false):
		var s := part.strip_edges()
		if not s.is_valid_float():
			return PackedFloat64Array()
		out.append(float(s))
	if out.size() == count or (allow_short and out.size() == count - 1):
		return out
	return PackedFloat64Array()


func _split_ints(text: String, count: int) -> PackedInt64Array:
	var clean := text.replace("(", "").replace(")", "").replace("[", "").replace("]", "")
	var out := PackedInt64Array()
	for part in clean.split(",", false):
		var s := part.strip_edges()
		if not s.is_valid_int():
			return PackedInt64Array()
		out.append(int(s))
	if out.size() == count:
		return out
	return PackedInt64Array()


# ---------------------------------------------------------------- requests ----

func _request_snapshot() -> void:
	if not _connected or plugin == null:
		return
	plugin.request_snapshot(session_id)


func _on_auto_timeout() -> void:
	if _auto_check.button_pressed:
		_request_snapshot()


func _on_auto_toggled(pressed: bool) -> void:
	# Reflect the checkbox immediately: stop the timer when auto-refresh is off
	# (a stopped timer stays stopped across a reconnect, matching the checkbox),
	# restart it when on. paused (set by set_connected) still gates the countdown.
	if _auto_timer:
		if pressed:
			_auto_timer.start()
		else:
			_auto_timer.stop()
	_request_snapshot()
