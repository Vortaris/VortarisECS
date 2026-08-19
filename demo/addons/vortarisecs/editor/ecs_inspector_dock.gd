@tool
extends VBoxContainer

## VortarisECS inspector dock (editor only).
##
## Shows a live browse of the VECS engine singleton world: entity / component /
## field tree, world stats, last query execution time, and a JSON snapshot dump.
## Every access guards on Engine.has_singleton("VECS") so the dock is a no-op in
## headless / non-editor runs where the singleton (or the plugin) is absent.
##
## IMPORTANT: because of process isolation, this dock can ONLY see the editor
## process's world — which is empty. It does NOT see a world built inside a
## running (F5) game. To debug the running game, use the remote monitor "ECS"
## tab in the editor debugger bottom panel (ecs_debugger_plugin.gd, shown while
## a game runs — the recommended GUI), the runtime overlay
## (addons/vortarisecs/ecs_overlay.gd, enabled with --vortaris-ecs-overlay on
## or the F2 key), the headless CLI args (--vortaris-ecs-stats /
## --vortaris-ecs-snapshot), or MCP run_script. See docs/AI_DEBUGGING.md.

var _world: VECSWorld = null

const TreeCopy := preload("res://addons/vortarisecs/editor/ecs_tree_copy.gd")

@onready var stats_label: Label = %StatsLabel
@onready var refresh_button: Button = %RefreshButton
@onready var entity_tree: Tree = %EntityTree
@onready var snapshot_label: Label = %SnapshotLabel
@onready var dump_button: Button = %DumpButton


func _ready() -> void:
	refresh_button.pressed.connect(_refresh)
	dump_button.pressed.connect(_dump_snapshot)
	# Issue #6: entity ids / component / field values must be copyable.
	entity_tree.set_meta("vecs_copy_helper", TreeCopy.new(entity_tree))
	# The two info lines are plain Labels (Godot Label has no text selection);
	# the entity tree's copy helper covers the ids / field values the issue asks
	# for. Stats stay a simple read-only readout.
	_resolve_world()


func _resolve_world() -> void:
	if not Engine.has_singleton("VECS"):
		stats_label.text = "VECS singleton unavailable (headless / not loaded)"
		_world = null
		return
	_world = Engine.get_singleton("VECS") as VECSWorld


func _refresh() -> void:
	_resolve_world()
	if _world == null:
		entity_tree.clear()
		return

	var stats: Dictionary = _world.get_debug_stats()
	stats_label.text = (
		"entities=%d  archetypes=%d  components=%d  observers=%d  pool=%d  change_tick=%d  query_cache=%d"
		% [
			stats.get("entity_count", 0),
			stats.get("archetype_count", 0),
			stats.get("component_count", 0),
			stats.get("observer_count", 0),
			stats.get("pool_size", 0),
			stats.get("change_tick", 0),
			stats.get("query_cache_entries", 0),
		]
	)

	entity_tree.clear()
	var root: TreeItem = entity_tree.create_item()
	root.set_text(0, "Entities (%d)" % _world.entity_count())

	# Query timing sample: measure a broad query for the stat line.
	var q := _world.query().with_all([])
	var before_usec := Time.get_ticks_usec()
	q.execute()
	var elapsed := Time.get_ticks_usec() - before_usec
	# with_all([]) is not a useful "all entities" query; count via entity_count
	# and report the last executed query time measured inside the query builder.
	snapshot_label.text = "last query exec: %d usec" % q.get_last_execution_time_usec()
	# ignore `elapsed` (kept for clarity); the C++ side reports its own timing

	var data: Array = _world.entities_to_data()
	for edata in data:
		var item: TreeItem = entity_tree.create_item(root)
		item.set_text(0, "id=%d" % int(edata.get("id", 0)))
		var comps: Dictionary = edata.get("components", {})
		for cname in comps:
			var comp_item: TreeItem = entity_tree.create_item(item)
			comp_item.set_text(0, cname)
			var fields: Dictionary = comps[cname]
			for fname in fields:
				var fitem: TreeItem = entity_tree.create_item(comp_item)
				fitem.set_text(0, "%s = %s" % [fname, str(fields[fname])])


func _dump_snapshot() -> void:
	_resolve_world()
	if _world == null:
		return
	# Honors vortarisecs/serialization/compact_json (compact vs pretty JSON).
	var text: String = _world.serialize_snapshot_json_string()
	print("[VortarisECS Inspector] snapshot: %s" % text)
	snapshot_label.text = "snapshot dumped (%d bytes of JSON)" % text.to_utf8_buffer().size()
