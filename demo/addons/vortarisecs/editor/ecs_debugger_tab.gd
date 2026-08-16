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
##
## UX conveniences (0.3.0):
##   U1  Every tree defaults COLLAPSED (only the top level is shown; click a
##       row to expand). Expanded/collapsed state survives refreshes and
##       re-sorts: identities are keyed by entity id / component / field name,
##       so a node the user opened stays open on the next snapshot. While a
##       filter is active the shown rows are force-expanded so the matches are
##       immediately visible; clearing the filter restores the pre-filter
##       expansion state.
##   U2  Clicking a column header sorts that page's top-level rows (ascending /
##       descending toggle). Godot's Tree has no built-in header sort, so the
##       raw snapshot data is kept around and re-sorted before re-populating.
##       The active column shows a "↑"/"↓" glyph in its title.
##   U3  Entities / Components / Systems each have a search LineEdit above the
##       tree; typing filters immediately (text_changed), clearing restores all.
##       Stats is intentionally kept filter-free.
##   W3  Entities page has a "Filter…" button that opens a component picker
##       (checkboxes from the snapshot's registered components) plus an All/Any
##       mode. The chosen component set is AND/OR-ed with the text search and
##       survives refresh/sort. A summary label + Clear button sit in the bar.
##   W4  The Entities search box has a mode dropdown: Mixed (id+component+value
##       substring), By value (field values, supports "comp/field == value"),
##       By component (names only) and Fuzzy (loose subsequence matching).
##   V2  No minimum-size restriction: the tab lays out at whatever size the
##       editor's debugger bottom panel is dragged to (the root Control's min
##       size is 0). The column minimum widths are just a sensible default.
##   V3  All four trees use VECSResizableTree so every column separator is
##       user-draggable (Godot's Tree has no built-in header drag-resize).
##   V4  The component-picker Filter dialog keeps the checkbox list inside a
##       ScrollContainer with a reasonable ~380px default height, so a project
##       with hundreds of registered components scrolls instead of stretching
##       the dialog off-screen.

var plugin: EditorDebuggerPlugin = null
var session_id: int = -1

const ResizableTree := preload("res://addons/vortarisecs/editor/ecs_resizable_tree.gd")

const DEFAULT_AUTO_REFRESH_INTERVAL := 1.0
const DEFAULT_MAX_ENTITIES := 500

## Default content size of the W3 component-picker dialog (V4). Tall enough to
## show a handful of checkboxes plus the mode row; the list scrolls past it.
const COMP_FILTER_DIALOG_MIN_SIZE := Vector2i(360, 380)

const ENTITY_COLS := ["Entity ID", "Component", "Field", "Value"]
const ENTITY_WIDTHS := [220, 120, 140, 200]
const COMPONENT_COLS := ["Component", "Field", "Type", "Count", "Sync", "Net"]
const COMPONENT_WIDTHS := [180, 120, 90, 60, 50, 50]
const SYSTEM_COLS := ["Name", "Group", "Active", "Paused", "Interval", "Flush"]
const SYSTEM_WIDTHS := [160, 110, 60, 60, 80, 60]
const STAT_COLS := ["Stat", "Value"]
const STAT_WIDTHS := [220, 160]

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
var _entities_filter: LineEdit
var _components_filter: LineEdit
var _systems_filter: LineEdit
var _auto_timer: Timer
var _connected := false

# Last snapshot payloads, kept so a header click (U2) or a search keystroke (U3)
# can re-render without waiting for the next snapshot.
var _last_stats: Variant = {}
var _last_components: Variant = []
var _last_systems: Variant = []
var _last_entities: Variant = []

# E1: the game caps the snapshot entity table at max_snapshot_entities and flags
# it with "truncated": true + "entity_total". Kept so the Entities page can show
# "truncated (N/total)" instead of silently hiding rows.
var _snapshot_truncated := false
var _snapshot_entity_total := 0

# E8 live edits that the game has not yet acked (W2). Keyed by
# "<eid>/<comp>/<field>" -> coerced Variant. Overlaid onto every incoming
# snapshot AND onto the local cache, so a re-render that fires between the edit
# and the confirming snapshot (auto-refresh tick, sort, filter) never rolls a
# just-edited cell back to the pre-edit value.
var _pending_edits := {}

# Per-page sort state: current column + ascending flag (U2).
var _entities_sort_col := 0
var _entities_sort_asc := true
var _components_sort_col := 0
var _components_sort_asc := true
var _systems_sort_col := 0
var _systems_sort_asc := true
var _stats_sort_col := 0
var _stats_sort_asc := true

# Per-page search text (U3).
var _entities_filter_text := ""
var _components_filter_text := ""
var _systems_filter_text := ""

# Entities-page search mode (W4): "mixed" | "value" | "component" | "fuzzy".
var _search_mode := "mixed"
var _entities_mode: OptionButton

# Entities-page component/archetype filter (W3): selected component names and
# the All/Any mode. "No selection = no filter". The filter window and its bar
# are built lazily in _make_entities_page()/dialog helpers.
var _entities_comp_filter := {}
var _entities_comp_filter_any := false
var _entities_filter_button: Button
var _entities_filter_summary: Label
var _entities_clear_filter_button: Button
var _comp_filter_dialog: AcceptDialog
var _comp_filter_box: VBoxContainer
var _comp_filter_option: OptionButton
var _comp_filter_checks := {}
var _comp_filter_list: VBoxContainer

# Per-page preserved expansion state (U1). Only updated from a NON-filtered
# render, so clearing a filter returns to exactly what the user had expanded
# before they started filtering. `_*_force_expanded` remembers the last render
# was filter-active (rows were force-expanded) so that state is never captured.
var _entities_expanded := {}
var _components_expanded := {}
var _systems_expanded := {}
var _stats_expanded := {}
var _entities_force_expanded := false
var _components_force_expanded := false
var _systems_force_expanded := false


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
		# Disconnect (session stopped / game ended): drop any in-flight live edits
		# from the previous game so a stale edit cannot be overlaid onto a NEW
		# session's snapshot after reconnect (E4).
		_pending_edits.clear()


func set_snapshot(snapshot: Dictionary) -> void:
	if _entities_tree == null:
		return
	# Keep the raw payloads so U2/U3 can re-render without a new snapshot.
	_last_stats = snapshot.get("stats", {})
	_last_components = snapshot.get("components", [])
	_last_systems = snapshot.get("systems", [])
	_last_entities = snapshot.get("entities", [])
	_snapshot_truncated = bool(snapshot.get("truncated", false))
	_snapshot_entity_total = int(snapshot.get("entity_total", (_last_entities as Array).size()))
	# A snapshot may have been computed before the game processed an in-flight
	# E8 edit (auto-refresh race). Re-apply unacked edits so a stale snapshot
	# does not roll the just-edited cell back to its pre-edit value (W2).
	_overlay_pending_edits()
	_render_stats()
	_render_components()
	_render_systems()
	_render_entities()


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

	_entities_tree = _make_tree(ENTITY_COLS, ENTITY_WIDTHS)
	_entities_tree.name = "EntitiesTree"
	_entities_tree.column_title_clicked.connect(_on_column_title_clicked.bind("entities"))
	_entities_filter = _make_search("Entities", "Filter by entity id or component…")
	_entities_filter.text_changed.connect(_on_filter_changed.bind("entities"))
	_tabs.add_child(_make_entities_page())

	_components_tree = _make_tree(COMPONENT_COLS, COMPONENT_WIDTHS)
	_components_tree.name = "ComponentsTree"
	_components_tree.column_title_clicked.connect(_on_column_title_clicked.bind("components"))
	_components_filter = _make_search("Components", "Filter by component name…")
	_components_filter.text_changed.connect(_on_filter_changed.bind("components"))
	_tabs.add_child(_make_page("Components", _components_tree, _components_filter))

	_systems_tree = _make_tree(SYSTEM_COLS, SYSTEM_WIDTHS)
	_systems_tree.name = "SystemsTree"
	_systems_tree.column_title_clicked.connect(_on_column_title_clicked.bind("systems"))
	_systems_filter = _make_search("Systems", "Filter by system name or group…")
	_systems_filter.text_changed.connect(_on_filter_changed.bind("systems"))
	_tabs.add_child(_make_page("Systems", _systems_tree, _systems_filter))

	_stats_tree = _make_tree(STAT_COLS, STAT_WIDTHS)
	_stats_tree.name = "StatsTree"
	_stats_tree.column_title_clicked.connect(_on_column_title_clicked.bind("stats"))
	_tabs.add_child(_make_page("Stats", _stats_tree))

	# Reflect the default sort (column 0, ascending) in the header titles.
	_refresh_column_titles(_entities_tree, ENTITY_COLS, _entities_sort_col, _entities_sort_asc)
	_refresh_column_titles(_components_tree, COMPONENT_COLS, _components_sort_col, _components_sort_asc)
	_refresh_column_titles(_systems_tree, SYSTEM_COLS, _systems_sort_col, _systems_sort_asc)
	_refresh_column_titles(_stats_tree, STAT_COLS, _stats_sort_col, _stats_sort_asc)


func _make_search(page_name: String, placeholder: String) -> LineEdit:
	var search := LineEdit.new()
	search.name = page_name + "Search"
	search.placeholder_text = placeholder
	search.clear_button_enabled = true
	search.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	return search


func _make_tree(column_titles: Array, widths: Array) -> Tree:
	var tree: Tree = ResizableTree.new()
	tree.size_flags_vertical = Control.SIZE_EXPAND_FILL
	tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	tree.column_titles_visible = true
	tree.hide_root = true
	tree.columns = column_titles.size()
	for i in column_titles.size():
		tree.set_column_title(i, column_titles[i])
		# Only the LAST column expands (V3); the earlier columns are non-expanding
		# so a VECSResizableTree drag pins their width exactly. A fully-expanding
		# first column would snap the drag back to fill the leftover space.
		tree.set_column_expand(i, i == column_titles.size() - 1)
		tree.set_column_custom_minimum_width(i, widths[i] if i < widths.size() else 80)
		# Never clip a dragged column's text (matches VECSResizableTree).
		tree.set_column_clip_content(i, false)
	return tree


func _make_page(title: String, tree: Tree, search: LineEdit = null) -> Control:
	var page := VBoxContainer.new()
	page.name = title
	page.add_theme_constant_override("separation", 4)
	# Horizontal rule under the tab bar so each page reads as
	# separator / search (optional) / table (matches the toolbar separator above
	# the tabs).
	var sep := HSeparator.new()
	sep.name = "PageSeparator"
	page.add_child(sep)
	if search != null:
		page.add_child(search)
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


## Entities page layout: separator / component-filter bar / search / table (W3).
## The extra bar holds the Filter… button (opens the component picker), a live
## summary of the active selection, and a Clear button.
func _make_entities_page() -> Control:
	var page := VBoxContainer.new()
	page.name = "Entities"
	page.add_theme_constant_override("separation", 4)
	var sep := HSeparator.new()
	sep.name = "PageSeparator"
	page.add_child(sep)

	var bar := HBoxContainer.new()
	bar.name = "ComponentFilterBar"
	bar.add_theme_constant_override("separation", 8)
	_entities_filter_button = Button.new()
	_entities_filter_button.name = "ComponentFilterButton"
	_entities_filter_button.text = "Filter…"
	_entities_filter_button.tooltip_text = "Show only entities that carry the selected components."
	_entities_filter_button.pressed.connect(_open_comp_filter_dialog)
	bar.add_child(_entities_filter_button)
	_entities_filter_summary = Label.new()
	_entities_filter_summary.name = "ComponentFilterSummary"
	bar.add_child(_entities_filter_summary)
	var spacer := Control.new()
	spacer.name = "ComponentFilterSpacer"
	spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	bar.add_child(spacer)
	_entities_clear_filter_button = Button.new()
	_entities_clear_filter_button.name = "ClearComponentFilterButton"
	_entities_clear_filter_button.text = "Clear"
	_entities_clear_filter_button.tooltip_text = "Clear the component filter and show all entities."
	_entities_clear_filter_button.pressed.connect(_on_clear_comp_filter)
	bar.add_child(_entities_clear_filter_button)
	page.add_child(bar)

	# Search mode dropdown (W4) sits beside the search box.
	var search_bar := HBoxContainer.new()
	search_bar.name = "SearchBar"
	search_bar.add_theme_constant_override("separation", 6)
	_entities_mode = OptionButton.new()
	_entities_mode.name = "SearchModeOption"
	_entities_mode.add_item("Mixed", 0)
	_entities_mode.add_item("By value", 1)
	_entities_mode.add_item("By component", 2)
	_entities_mode.add_item("Fuzzy", 3)
	_entities_mode.select(0)
	_entities_mode.item_selected.connect(_on_search_mode_changed)
	_entities_mode.tooltip_text = "Search mode: Mixed (id+component+value), By value (fields, supports 'comp/field == value'), By component (names only), Fuzzy (loose)."
	search_bar.add_child(_entities_mode)
	_entities_filter.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	search_bar.add_child(_entities_filter)
	page.add_child(search_bar)
	var margin := MarginContainer.new()
	margin.name = "TreeMargin"
	margin.size_flags_vertical = Control.SIZE_EXPAND_FILL
	margin.add_theme_constant_override("margin_left", 4)
	margin.add_theme_constant_override("margin_right", 4)
	margin.add_theme_constant_override("margin_top", 4)
	margin.add_theme_constant_override("margin_bottom", 4)
	margin.add_child(_entities_tree)
	page.add_child(margin)
	_refresh_comp_filter_summary()
	return page


# ---------------------------------------------------------------- rendering ----
# Each _render_* function:
#   1. preserves the user's expansion state (unless the previous render was
#      force-expanded by an active filter — U1);
#   2. filters the raw snapshot payload by the page's search text (U3);
#   3. sorts the surviving rows by the page's sort column/direction (U2);
#   4. re-populates the tree, tagging every row with an identity metadata so the
#      expansion state survives the rebuild;
#   5. re-applies collapse state (default collapsed, or force-expanded while a
#      filter is active).

func _render_stats() -> void:
	if _stats_tree == null:
		return
	_stats_expanded = _capture_expansion(_stats_tree)
	_stats_tree.clear()
	var root := _stats_tree.create_item()
	var stats: Variant = _last_stats
	if not (stats is Dictionary):
		_placeholder_item(root, "no stats")
		return
	var pairs := []
	for key in (stats as Dictionary).keys():
		pairs.append({"k": str(key), "v": (stats as Dictionary)[key]})
	_sort_data(pairs, _stats_sort_col, _stats_sort_asc, _stats_sort_key)
	for p in pairs:
		var item := _stats_tree.create_item(root)
		item.set_text(0, str(p["k"]))
		item.set_text(1, str(p["v"]))
		item.set_metadata(0, {"name": str(p["k"])})
	_apply_collapse_state(_stats_tree, _stats_expanded, false)


func _render_components() -> void:
	if _components_tree == null:
		return
	var filter := _components_filter_text.strip_edges().to_lower()
	var filter_active := filter != ""
	if not filter_active and not _components_force_expanded:
		_components_expanded = _capture_expansion(_components_tree)
	_components_force_expanded = filter_active
	_components_tree.clear()
	var root := _components_tree.create_item()
	var components: Variant = _last_components
	if not (components is Array) or (components as Array).size() == 0:
		_placeholder_item(root, "no components registered")
		return
	var visible: Array = []
	for cdata in components:
		if not (cdata is Dictionary):
			continue
		if _component_matches(cdata, filter):
			visible.append(cdata)
	if visible.size() == 0:
		_placeholder_item(root, "no components match filter")
		return
	_sort_data(visible, _components_sort_col, _components_sort_asc, _component_sort_key)
	for cdata in visible:
		var cname := str(cdata.get("name", "?"))
		var item := _components_tree.create_item(root)
		item.set_text(0, cname)
		item.set_metadata(0, {"comp": cname})
		item.set_text(1, "size=%d" % int(cdata.get("size", 0)))
		var fields: Variant = cdata.get("fields", [])
		if fields is Array:
			for fd in fields:
				if not (fd is Dictionary):
					continue
				var fname := str(fd.get("name", ""))
				var fitem := _components_tree.create_item(item)
				fitem.set_text(1, fname)
				fitem.set_text(2, str(fd.get("type", "")))
				fitem.set_text(3, str(fd.get("count", 1)))
				fitem.set_text(4, str(fd.get("sync_priority", "")))
				fitem.set_text(5, str(fd.get("networked", "")))
				fitem.set_metadata(0, {"comp": cname, "field": fname})
	_apply_collapse_state(_components_tree, _components_expanded, filter_active)


func _render_systems() -> void:
	if _systems_tree == null:
		return
	var filter := _systems_filter_text.strip_edges().to_lower()
	var filter_active := filter != ""
	if not filter_active and not _systems_force_expanded:
		_systems_expanded = _capture_expansion(_systems_tree)
	_systems_force_expanded = filter_active
	_systems_tree.clear()
	var root := _systems_tree.create_item()
	var systems: Variant = _last_systems
	if not (systems is Array) or (systems as Array).size() == 0:
		_placeholder_item(root, "no systems registered")
		return
	var visible: Array = []
	for sdata in systems:
		if not (sdata is Dictionary):
			continue
		if _system_matches(sdata, filter):
			visible.append(sdata)
	if visible.size() == 0:
		_placeholder_item(root, "no systems match filter")
		return
	_sort_data(visible, _systems_sort_col, _systems_sort_asc, _system_sort_key)
	for sdata in visible:
		var sname := str(sdata.get("name", "?"))
		var item := _systems_tree.create_item(root)
		item.set_text(0, sname)
		item.set_metadata(0, {"name": sname})
		item.set_text(1, str(sdata.get("group", "")))
		item.set_text(2, "true" if bool(sdata.get("active", true)) else "false")
		item.set_text(3, "true" if bool(sdata.get("paused", false)) else "false")
		item.set_text(4, str(sdata.get("tick_interval", 0.0)))
		item.set_text(5, str(sdata.get("flush_mode", 0)))
	_apply_collapse_state(_systems_tree, _systems_expanded, filter_active)


func _render_entities() -> void:
	if _entities_tree == null:
		return
	var filter := _entities_filter_text.strip_edges().to_lower()
	var filter_active := filter != ""
	if not filter_active and not _entities_force_expanded:
		_entities_expanded = _capture_expansion(_entities_tree)
	_entities_force_expanded = filter_active
	_entities_tree.clear()
	var root := _entities_tree.create_item()
	var entities: Variant = _last_entities
	if not (entities is Array) or (entities as Array).size() == 0:
		_placeholder_item(root, "no entities")
		return
	var visible: Array = []
	for edata in entities:
		if not (edata is Dictionary):
			continue
		if _entity_matches(edata, filter) and _entity_passes_comp_filter(edata):
			visible.append(edata)
	if visible.size() == 0:
		_placeholder_item(root, "no entities match filter")
		return
	_sort_data(visible, _entities_sort_col, _entities_sort_asc, _entity_sort_key)
	var count := 0
	var total := visible.size()
	var note_shown := false
	for edata in visible:
		if count >= _max_entities:
			if not note_shown:
				var note := _entities_tree.create_item(root)
				note.set_text(0, "… %d more entities not shown (limit %d)" % [total - count, _max_entities])
				note_shown = true
			break
		count += 1
		var eid := int(edata.get("id", 0))
		var eid_text := "Entity #%d" % eid
		# An id match shows the whole entity; otherwise only the matching
		# component subtree(s) are shown (so filtering stays tight). "By value"
		# narrows further to the exact matching fields (W4).
		var id_matches := (not filter_active) or eid_text.to_lower().contains(filter)
		var eitem := _entities_tree.create_item(root)
		eitem.set_text(0, eid_text)
		eitem.set_metadata(0, {"eid": eid})
		var comps: Variant = edata.get("components", {})
		if not (comps is Dictionary):
			continue
		for cname in comps:
			if filter_active and not id_matches and not _comp_shows_match(edata, str(cname), filter):
				continue
			var citem := _entities_tree.create_item(eitem)
			citem.set_text(1, str(cname))
			citem.set_metadata(0, {"eid": eid, "comp": str(cname)})
			var fields: Variant = (comps as Dictionary)[cname]
			if fields is Dictionary:
				for fname in fields:
					var raw_value: Variant = (fields as Dictionary)[fname]
					if filter_active and not id_matches and not _field_shows_match(str(cname), str(fname), raw_value, filter):
						continue
					var fitem := _entities_tree.create_item(citem)
					fitem.set_text(2, str(fname))
					fitem.set_text(3, str(raw_value))
					# Runtime-debug editing (E8): the Value cell is editable and
					# carries the original Variant + addressing metadata, so the
					# edited text can be coerced back to the field's real type
					# before it is sent to the running game.
					fitem.set_editable(3, true)
					fitem.set_metadata(0, {
						"eid": eid,
						"comp": str(cname),
						"field": str(fname),
						"value": raw_value,
					})
	if _snapshot_truncated and not note_shown:
		# E1: the game capped the entity table before sending; show how many of
		# the world's total entities made it into this snapshot.
		var note := _entities_tree.create_item(root)
		note.set_text(0, "… snapshot truncated: %d of %d entities shown (limit %d)" % [count, _snapshot_entity_total, _max_entities])
	_apply_collapse_state(_entities_tree, _entities_expanded, filter_active)


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


# ------------------------------------------------------ U1 collapse helpers ----
# Expansion state is keyed by row identity (entity id / component / field), so
# it survives the full rebuild on every refresh, sort and filter change.

func _capture_expansion(tree: Tree) -> Dictionary:
	var expanded := {}
	var root := tree.get_root()
	if root == null:
		return expanded
	for child in root.get_children():
		_collect_expansion(child, expanded)
	return expanded


func _collect_expansion(item: TreeItem, expanded: Dictionary) -> void:
	if not item.collapsed:
		expanded[_item_identity(item)] = true
	for child in item.get_children():
		_collect_expansion(child, expanded)


func _apply_collapse_state(tree: Tree, expanded: Dictionary, force_expand: bool) -> void:
	var root := tree.get_root()
	if root == null:
		return
	for child in root.get_children():
		_set_collapse(child, expanded, force_expand)


func _set_collapse(item: TreeItem, expanded: Dictionary, force_expand: bool) -> void:
	if force_expand:
		item.collapsed = false
	else:
		# Default is collapsed (U1); only rows the user expanded stay open.
		item.collapsed = not expanded.has(_item_identity(item))
	for child in item.get_children():
		_set_collapse(child, expanded, force_expand)


## Stable identity of a row across rebuilds. Rows carry a Dictionary in
## metadata(0) with the addressing keys; anything else falls back to text(0).
func _item_identity(item: TreeItem) -> String:
	var meta: Variant = item.get_metadata(0)
	if meta is Dictionary:
		var d: Dictionary = meta
		if d.has("eid") and d.has("comp") and d.has("field"):
			return "e%d/c%s/f%s" % [int(d["eid"]), str(d["comp"]), str(d["field"])]
		if d.has("eid") and d.has("comp"):
			return "e%d/c%s" % [int(d["eid"]), str(d["comp"])]
		if d.has("eid"):
			return "e%d" % int(d["eid"])
		if d.has("comp") and d.has("field"):
			return "c%s/f%s" % [str(d["comp"]), str(d["field"])]
		if d.has("comp"):
			return "c%s" % str(d["comp"])
		if d.has("name"):
			return "n%s" % str(d["name"])
	return "t%s" % item.get_text(0)


# ------------------------------------------------------- U2 sorting helpers ----
# Godot's Tree has no built-in header sort; we sort the raw rows ourselves and
# re-populate. Sorting applies to each page's top-level rows.

func _on_column_title_clicked(col: int, _mouse_button_index: int, page: String) -> void:
	match page:
		"entities":
			var cyc := _cycle_sort(_entities_sort_col, _entities_sort_asc, col)
			_entities_sort_col = cyc[0]
			_entities_sort_asc = cyc[1]
			_refresh_column_titles(_entities_tree, ENTITY_COLS, _entities_sort_col, _entities_sort_asc)
			_render_entities()
		"components":
			var cyc := _cycle_sort(_components_sort_col, _components_sort_asc, col)
			_components_sort_col = cyc[0]
			_components_sort_asc = cyc[1]
			_refresh_column_titles(_components_tree, COMPONENT_COLS, _components_sort_col, _components_sort_asc)
			_render_components()
		"systems":
			var cyc := _cycle_sort(_systems_sort_col, _systems_sort_asc, col)
			_systems_sort_col = cyc[0]
			_systems_sort_asc = cyc[1]
			_refresh_column_titles(_systems_tree, SYSTEM_COLS, _systems_sort_col, _systems_sort_asc)
			_render_systems()
		"stats":
			var cyc := _cycle_sort(_stats_sort_col, _stats_sort_asc, col)
			_stats_sort_col = cyc[0]
			_stats_sort_asc = cyc[1]
			_refresh_column_titles(_stats_tree, STAT_COLS, _stats_sort_col, _stats_sort_asc)
			_render_stats()


func _cycle_sort(cur_col: int, cur_asc: bool, col: int) -> Array:
	if col == cur_col:
		return [col, not cur_asc]
	return [col, true]


func _refresh_column_titles(tree: Tree, titles: Array, sort_col: int, sort_asc: bool) -> void:
	for i in titles.size():
		var t: String = titles[i]
		if i == sort_col:
			t += " ↑" if sort_asc else " ↓"
		tree.set_column_title(i, t)


## Re-sorts `data` (mutated in place) by the given column via `key_func`.
## `key_func.call(row, col)` returns the sort key for that row. Ties keep the
## original relative order (stable), so repeated clicks don't jitter.
func _sort_data(data: Array, col: int, ascending: bool, key_func: Callable) -> void:
	if data.size() < 2:
		return
	var rows := []
	for i in data.size():
		rows.append({"i": i, "k": key_func.call(data[i], col)})
	rows.sort_custom(_sort_rows.bind(ascending))
	var original := data.duplicate()
	for i in rows.size():
		data[i] = original[int(rows[i]["i"])]


func _sort_rows(a: Dictionary, b: Dictionary, ascending: bool) -> bool:
	if ascending:
		if _sort_less(a["k"], b["k"]):
			return true
		if _sort_less(b["k"], a["k"]):
			return false
	else:
		if _sort_less(b["k"], a["k"]):
			return true
		if _sort_less(a["k"], b["k"]):
			return false
	return int(a["i"]) < int(b["i"])


## Numeric-aware comparison: numbers (and numeric strings / bools) compare
## numerically, everything else lexicographically.
func _sort_less(a: Variant, b: Variant) -> bool:
	var an := _to_sort_number(a)
	var bn := _to_sort_number(b)
	if an != null and bn != null:
		return an < bn
	return str(a) < str(b)


func _to_sort_number(v: Variant) -> Variant:
	match typeof(v):
		TYPE_INT:
			return float(v)
		TYPE_FLOAT:
			return v
		TYPE_BOOL:
			return 1.0 if v else 0.0
		TYPE_STRING:
			var s: String = (v as String).strip_edges()
			if s.is_valid_float():
				return float(s)
			return null
	return null


# Per-page sort keys. The Entities page's columns 1/2 are child-level in the
# tree, so at the top level they sort by aggregate counts (components / fields)
# and column 3 (Value) falls back to the entity id.
func _entity_sort_key(edata: Dictionary, col: int) -> Variant:
	match col:
		0:
			return int(edata.get("id", 0))
		1:
			var comps: Variant = edata.get("components", {})
			return (comps as Dictionary).size() if comps is Dictionary else 0
		2:
			var comps: Variant = edata.get("components", {})
			var n := 0
			if comps is Dictionary:
				for cname in comps:
					var fields: Variant = (comps as Dictionary)[cname]
					if fields is Dictionary:
						n += (fields as Dictionary).size()
			return n
		_:
			return int(edata.get("id", 0))


func _component_sort_key(cdata: Dictionary, col: int) -> Variant:
	match col:
		0:
			return str(cdata.get("name", ""))
		1:
			return int(cdata.get("size", 0))
		_:
			return str(cdata.get("name", ""))


func _system_sort_key(sdata: Dictionary, col: int) -> Variant:
	match col:
		0:
			return str(sdata.get("name", ""))
		1:
			return str(sdata.get("group", ""))
		2:
			return bool(sdata.get("active", true))
		3:
			return bool(sdata.get("paused", false))
		4:
			return float(sdata.get("tick_interval", 0.0))
		5:
			return int(sdata.get("flush_mode", 0))
		_:
			return str(sdata.get("name", ""))


func _stats_sort_key(pair: Dictionary, col: int) -> Variant:
	return pair["k"] if col == 0 else pair["v"]


# ------------------------------------------------------- U3 filter helpers ----
# Each page stores its search text in `_*_filter_text`; the matching helpers
# below run at render time so filtering takes effect immediately on keystroke.

func _on_filter_changed(text: String, page: String) -> void:
	match page:
		"entities":
			_entities_filter_text = text
			_render_entities()
		"components":
			_components_filter_text = text
			_render_components()
		"systems":
			_systems_filter_text = text
			_render_systems()


func _on_search_mode_changed(index: int) -> void:
	match index:
		0:
			_search_mode = "mixed"
		1:
			_search_mode = "value"
		2:
			_search_mode = "component"
		3:
			_search_mode = "fuzzy"
	_render_entities()


## Entity-level visibility for the active search mode (W4). `filter` is already
## lower-cased by the caller. Mixed searches id + component name + field value;
## By value searches field values (with `comp/field == value` support); By
## component searches component names only; Fuzzy adds loose subsequence matching.
func _entity_matches(edata: Dictionary, filter: String) -> bool:
	if filter == "":
		return true
	match _search_mode:
		"component":
			return _entity_has_comp_name_match(edata, filter)
		"value":
			return _entity_has_value_match(edata, filter)
		"fuzzy":
			return _entity_mixed_match(edata, filter) or _entity_fuzzy_match(edata, filter)
		_:
			return _entity_mixed_match(edata, filter)


func _entity_mixed_match(edata: Dictionary, filter: String) -> bool:
	var eid := int(edata.get("id", 0))
	if ("entity #%d" % eid).contains(filter):
		return true
	var comps: Variant = edata.get("components", {})
	if comps is Dictionary:
		for cname in comps:
			if str(cname).to_lower().contains(filter):
				return true
			var fields: Variant = (comps as Dictionary)[cname]
			if fields is Dictionary:
				for fname in fields:
					if str((fields as Dictionary)[fname]).to_lower().contains(filter):
						return true
	return false


func _entity_has_comp_name_match(edata: Dictionary, filter: String) -> bool:
	var comps: Variant = edata.get("components", {})
	if comps is Dictionary:
		for cname in comps:
			if str(cname).to_lower().contains(filter):
				return true
	return false


func _entity_has_value_match(edata: Dictionary, filter: String) -> bool:
	var q := _parse_value_query(filter)
	var comps: Variant = edata.get("components", {})
	if comps is Dictionary:
		for cname in comps:
			if q["comp"] != "" and q["comp"] != str(cname).to_lower():
				continue
			var fields: Variant = (comps as Dictionary)[cname]
			if fields is Dictionary:
				for fname in fields:
					if q["field"] != "" and q["field"] != str(fname).to_lower():
						continue
					if str((fields as Dictionary)[fname]).to_lower().contains(q["value"]):
						return true
	return false


func _entity_fuzzy_match(edata: Dictionary, filter: String) -> bool:
	# Loose fallback: the filter appears as a (case-insensitive) subsequence of
	# the entity's id + component names + field names + field values.
	var hay := "entity #%d" % int(edata.get("id", 0))
	var comps: Variant = edata.get("components", {})
	if comps is Dictionary:
		for cname in comps:
			hay += " " + str(cname).to_lower()
			var fields: Variant = (comps as Dictionary)[cname]
			if fields is Dictionary:
				for fname in fields:
					hay += " " + str(fname).to_lower() + " " + str((fields as Dictionary)[fname]).to_lower()
	return _is_subsequence(filter, hay)


func _is_subsequence(needle: String, haystack: String) -> bool:
	if needle.is_empty():
		return true
	var n := 0
	for i in haystack.length():
		if haystack[i] == needle[n]:
			n += 1
			if n == needle.length():
				return true
	return false


## Parses a By-value query: "comp/field == value", "field == value", or a bare
## value. Returns { "comp", "field", "value" } with "" meaning "any".
func _parse_value_query(text: String) -> Dictionary:
	var out := {"comp": "", "field": "", "value": text}
	var eq := text.find("==")
	if eq == -1:
		return out
	var lhs := text.substr(0, eq).strip_edges().to_lower()
	var rhs := text.substr(eq + 2).strip_edges().to_lower()
	var slash := lhs.find("/")
	if slash != -1:
		out["comp"] = lhs.substr(0, slash).strip_edges()
		out["field"] = lhs.substr(slash + 1).strip_edges()
	else:
		out["field"] = lhs
	out["value"] = rhs
	return out


## Should the component `cname` subtree of `edata` be shown when the search is
## active and the entity id did not match? (W4)
func _comp_shows_match(edata: Dictionary, cname: String, filter: String) -> bool:
	match _search_mode:
		"component":
			return cname.to_lower().contains(filter)
		"value":
			return _comp_has_matching_field(edata, cname, filter)
		_:
			# mixed / fuzzy: component name or any field value matches
			if cname.to_lower().contains(filter):
				return true
			var comps: Variant = edata.get("components", {})
			if comps is Dictionary and (comps as Dictionary).has(cname):
				var fields: Variant = (comps as Dictionary)[cname]
				if fields is Dictionary:
					for fname in fields:
						if str((fields as Dictionary)[fname]).to_lower().contains(filter):
							return true
			return false


func _comp_has_matching_field(edata: Dictionary, cname: String, filter: String) -> bool:
	var q := _parse_value_query(filter)
	if q["comp"] != "" and q["comp"] != cname.to_lower():
		return false
	var comps: Variant = edata.get("components", {})
	if comps is Dictionary and (comps as Dictionary).has(cname):
		var fields: Variant = (comps as Dictionary)[cname]
		if fields is Dictionary:
			for fname in fields:
				if _field_value_matches(cname, str(fname), (fields as Dictionary)[fname], q):
					return true
	return false


## Should the field row be shown when narrowing is active? Only By-value mode
## narrows to the exact matching fields; the other modes show the whole comp
## subtree once the comp itself matched.
func _field_shows_match(cname: String, fname: String, raw_value: Variant, filter: String) -> bool:
	if _search_mode == "value":
		return _field_value_matches(cname, fname, raw_value, _parse_value_query(filter))
	return true


func _field_value_matches(cname: String, fname: String, raw_value: Variant, q: Dictionary) -> bool:
	if q["comp"] != "" and q["comp"] != cname.to_lower():
		return false
	if q["field"] != "" and q["field"] != fname.to_lower():
		return false
	return str(raw_value).to_lower().contains(q["value"])


func _component_matches(cdata: Dictionary, filter: String) -> bool:
	if filter == "":
		return true
	return str(cdata.get("name", "")).to_lower().contains(filter)


func _system_matches(sdata: Dictionary, filter: String) -> bool:
	if filter == "":
		return true
	return str(sdata.get("name", "")).to_lower().contains(filter) \
			or str(sdata.get("group", "")).to_lower().contains(filter)


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
		# Unparsable edits keep the last known good text (E8): never send a coerced
		# false just because Godot accepted a garbage bool literal. Hint the user
		# when a bool cell received text that is not true/false/1/0.
		if typeof(original) == TYPE_BOOL and not _bool_text_valid(item.get_text(col)):
			_status_label.text = "Invalid boolean for %s.%s on #%d (use true/false/1/0)" % [comp, field, eid]
			_status_label.add_theme_color_override("font_color", Color(0.9, 0.5, 0.4))
		return  # unparsable / unchanged — keep the last known good text
	# W2: keep this edit in flight locally so a re-render that arrives before the
	# confirming snapshot (auto-refresh tick, sort, filter) shows the new value
	# instead of rolling the cell back to the pre-edit snapshot value.
	_pending_edits["%d/%s/%s" % [eid, comp, field]] = value
	_overlay_pending_edits()
	# Keep the row's own metadata in sync so a follow-up edit coerces against the
	# value the user actually set, not the stale pre-edit snapshot value.
	meta["value"] = value
	item.set_metadata(0, meta)
	_send_set_field(eid, comp, field, value)


func _send_set_field(entity_id: int, comp: String, field: String, value: Variant) -> void:
	if not _connected or plugin == null:
		return
	plugin.send_set_field(session_id, entity_id, comp, field, value)
	_status_label.text = "Set %s.%s on #%d — sent…" % [comp, field, entity_id]
	_status_label.add_theme_color_override("font_color", Color(0.9, 0.85, 0.5))


func set_field_result(ok: bool, entity_id: int, comp: String, field: String, error: String) -> void:
	# The game answered; the write is settled. Stop overlaying this edit so the
	# next snapshot (which now carries the real value) is rendered as-is.
	_pending_edits.erase("%d/%s/%s" % [entity_id, comp, field])
	if ok:
		_status_label.text = "Set %s.%s on #%d — applied" % [comp, field, entity_id]
		_status_label.add_theme_color_override("font_color", Color(0.45, 0.9, 0.45))
	else:
		_status_label.text = "Set %s.%s on #%d failed: %s" % [comp, field, entity_id, error]
		_status_label.add_theme_color_override("font_color", Color(0.9, 0.5, 0.4))
	# Refresh on success (confirm) AND on failure (the optimistic value may not
	# have been written, so pull the world's real value back into the cell).
	_request_snapshot()


## True when `text` is an accepted bool cell literal: true/false/1/0,
## case-insensitive (E8). Anything else is refused rather than coerced to false.
func _bool_text_valid(text: String) -> bool:
	var s := text.strip_edges().to_lower()
	return s == "true" or s == "false" or s == "1" or s == "0"


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
			# Strict (E8): only true/false/1/0 parse; anything else keeps the
			# original value so the caller never sends a silently-coerced false.
			var s := text.strip_edges().to_lower()
			if s == "true" or s == "1":
				return true
			if s == "false" or s == "0":
				return false
			return original
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


## Re-applies every unacked E8 edit onto `_last_entities` in place. Called after
## an edit is recorded (so the local cache reflects the new value immediately)
## and after every incoming snapshot (so a stale pre-edit snapshot does not roll
## the cell back). O(#entities) — cheap next to the snapshot walk itself (W2).
func _overlay_pending_edits() -> void:
	if _pending_edits.is_empty():
		return
	var entities: Variant = _last_entities
	if not (entities is Array):
		return
	for edata in entities:
		if not (edata is Dictionary):
			continue
		var eid := int(edata.get("id", 0))
		var comps: Variant = edata.get("components", {})
		if not (comps is Dictionary):
			continue
		for cname in comps:
			var fields: Variant = (comps as Dictionary)[cname]
			if not (fields is Dictionary):
				continue
			for fname in fields:
				var key := "%d/%s/%s" % [eid, str(cname), str(fname)]
				if _pending_edits.has(key):
					(fields as Dictionary)[fname] = _pending_edits[key]


# ------------------------------------------------------- W3 component filter ----

func _open_comp_filter_dialog() -> void:
	if _comp_filter_dialog == null:
		_comp_filter_dialog = AcceptDialog.new()
		_comp_filter_dialog.name = "ComponentFilterDialog"
		_comp_filter_dialog.title = "Filter Entities by Component"
		_comp_filter_dialog.ok_button_text = "Apply"
		_comp_filter_dialog.confirmed.connect(_on_comp_filter_confirmed)
		_comp_filter_dialog.canceled.connect(_on_comp_filter_cancelled)
		_comp_filter_box = VBoxContainer.new()
		_comp_filter_box.name = "ComponentFilterBox"
		_comp_filter_box.add_theme_constant_override("separation", 4)
		# V4: cap the dialog's default height so a project with many components
		# scrolls instead of stretching the window off-screen. The ScrollContainer
		# below clips its (possibly very tall) list, so this stays the content min.
		_comp_filter_box.custom_minimum_size = COMP_FILTER_DIALOG_MIN_SIZE
		_comp_filter_dialog.add_child(_comp_filter_box)
		# All/Any mode selector — built once, selection refreshed per open.
		var header := HBoxContainer.new()
		header.name = "ModeRow"
		var mode_label := Label.new()
		mode_label.text = "Match:"
		header.add_child(mode_label)
		_comp_filter_option = OptionButton.new()
		_comp_filter_option.name = "ModeOption"
		_comp_filter_option.add_item("All selected components", 0)
		_comp_filter_option.add_item("Any selected component", 1)
		header.add_child(_comp_filter_option)
		_comp_filter_box.add_child(header)
		_comp_filter_box.add_child(HSeparator.new())
		# V4: the checkbox list lives in a ScrollContainer. Its minimum size does
		# NOT grow with the (tall) list, so the dialog keeps the size above.
		var scroll := ScrollContainer.new()
		scroll.name = "ComponentFilterScroll"
		scroll.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
		_comp_filter_box.add_child(scroll)
		_comp_filter_list = VBoxContainer.new()
		_comp_filter_list.name = "ComponentFilterList"
		_comp_filter_list.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		_comp_filter_list.add_theme_constant_override("separation", 2)
		scroll.add_child(_comp_filter_list)
		add_child(_comp_filter_dialog)
	_populate_comp_filter_dialog()
	_comp_filter_dialog.popup_centered(COMP_FILTER_DIALOG_MIN_SIZE)


func _populate_comp_filter_dialog() -> void:
	for child in _comp_filter_list.get_children():
		_comp_filter_list.remove_child(child)
		child.queue_free()
	_comp_filter_checks = {}
	# Reflect the current All/Any mode every time the dialog opens.
	_comp_filter_option.select(1 if _entities_comp_filter_any else 0)
	# One checkbox per registered component (from the snapshot's components data).
	var comps: Variant = _last_components
	var added := false
	if comps is Array:
		for cdata in comps:
			if not (cdata is Dictionary):
				continue
			var cname := str(cdata.get("name", ""))
			if cname == "":
				continue
			var cb := CheckBox.new()
			cb.text = cname
			cb.button_pressed = _entities_comp_filter.has(cname)
			_comp_filter_list.add_child(cb)
			_comp_filter_checks[cname] = cb
			added = true
	if not added:
		var none := Label.new()
		none.text = "No components registered yet."
		_comp_filter_list.add_child(none)


func _on_comp_filter_confirmed() -> void:
	_entities_comp_filter_any = _comp_filter_option.get_selected_id() == 1
	_entities_comp_filter.clear()
	for cname in _comp_filter_checks:
		if (_comp_filter_checks[cname] as CheckBox).button_pressed:
			_entities_comp_filter[cname] = true
	_refresh_comp_filter_summary()
	_render_entities()


func _on_comp_filter_cancelled() -> void:
	# Nothing to undo: the dialog is repopulated from state on every open.
	pass


func _on_clear_comp_filter() -> void:
	_entities_comp_filter.clear()
	_entities_comp_filter_any = false
	_refresh_comp_filter_summary()
	_render_entities()


func _refresh_comp_filter_summary() -> void:
	if _entities_filter_summary == null:
		return
	var n := _entities_comp_filter.size()
	if n == 0:
		_entities_filter_summary.text = "No component filter"
	else:
		var mode := "Any" if _entities_comp_filter_any else "All"
		_entities_filter_summary.text = "%d component%s selected (%s)" % [n, "s" if n != 1 else "", mode]


## W3 predicate: does this entity satisfy the active component filter? An empty
## selection means no filter. All-mode requires every selected component; Any-mode
## requires at least one.
func _entity_passes_comp_filter(edata: Dictionary) -> bool:
	if _entities_comp_filter.is_empty():
		return true
	var comps: Variant = edata.get("components", {})
	var names := {}
	if comps is Dictionary:
		for cname in comps:
			names[str(cname)] = true
	if _entities_comp_filter_any:
		for cname in _entities_comp_filter:
			if names.has(cname):
				return true
		return false
	for cname in _entities_comp_filter:
		if not names.has(cname):
			return false
	return true


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
