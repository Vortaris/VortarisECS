@tool
extends RefCounted
## Clipboard copy support for editor Trees (issue #6).
##
## Godot 4 Trees have no built-in copy. Attach one instance per Tree:
##
##     var copy := VECSTreeCopyScript.new(tree)   # or preload + new
##     tree.set_meta("vecs_copy_helper", copy)    # keep it referenced
##
## Adds:
##   - Ctrl+C / Cmd+C: copies the selected cell text
##   - Right-click on an item: popup with "Copy Cell" / "Copy Row" (row = all
##     columns joined with tabs)
##
## The helper is RefCounted; the gui_input signal connection (a bound Callable)
## plus the meta reference keep it alive exactly as long as the Tree lives.

var _tree: Tree
var _menu: PopupMenu
var _menu_item: TreeItem = null
var _menu_col := 0


func _init(p_tree: Tree) -> void:
	_tree = p_tree
	if _tree.focus_mode == Control.FOCUS_NONE:
		_tree.focus_mode = Control.FOCUS_ALL
	_tree.gui_input.connect(_on_gui_input)
	_menu = PopupMenu.new()
	_menu.add_item("Copy Cell", 0)
	_menu.add_item("Copy Row", 1)
	_menu.id_pressed.connect(_on_menu_id)
	_tree.add_child(_menu)


func _on_gui_input(event: InputEvent) -> void:
	if event is InputEventKey:
		var key := event as InputEventKey
		if key.pressed and not key.echo and key.keycode == KEY_C \
				and (key.ctrl_pressed or key.meta_pressed):
			_copy_selected_cell()
			_tree.accept_event()
	elif event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
			var item := _tree.get_item_at_position(mb.position)
			if item == null:
				return
			_menu_item = item
			_menu_col = maxi(0, _tree.get_column_at_position(mb.position))
			_menu.popup(Rect2(mb.global_position, Vector2.ZERO))
			_tree.accept_event()


func _copy_selected_cell() -> void:
	var item := _tree.get_selected()
	if item == null:
		return
	var col := _tree.get_selected_column()
	if col < 0:
		col = 0
	DisplayServer.clipboard_set(item.get_text(col))


func _on_menu_id(id: int) -> void:
	if _menu_item == null or not is_instance_valid(_menu_item):
		return
	match id:
		0:
			DisplayServer.clipboard_set(_menu_item.get_text(_menu_col))
		1:
			var parts: PackedStringArray = PackedStringArray()
			for c in _tree.columns:
				parts.append(_menu_item.get_text(c))
			DisplayServer.clipboard_set("\t".join(parts))
