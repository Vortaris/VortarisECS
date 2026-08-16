@tool
class_name VECSResizableTree
extends Tree
## Tree with user-draggable column separators.
##
## Godot's Tree has **no built-in** column-header drag-resize (verified on 4.7).
## This subclass listens on the `gui_input` signal (the native Tree handling keeps
## running, so selection / drag-reorder / header-click sort stay untouched) and
## intercepts press/drag on the column separators, pinning the dragged column's
## width via set_column_custom_minimum_width. Non-expanding columns (see
## set_column_expand) honour the pinned width exactly; the last expanding column
## absorbs the remainder. Column content is never clipped
## (set_column_clip_content(false)) so a dragged column shows its full text.
##
## Adapted from VortarisModLoader's VMLResizableTree so the VortarisECS remote
## monitor's Entities/Components/Systems/Stats trees get the same drag-resize (V3).
##
## The separator hit-test is **full-column-height**: pressing a separator anywhere
## in the tree (header OR a body row) starts the resize, not just in the header.
## The hit is decided purely by the mouse x near a column boundary, so rows
## themselves stay selectable / drag-reorderable everywhere except the ~6px
## separator band. Accepting the press event on a separator prevents the native
## Tree drag-reorder from also starting.

const SEPARATOR_TOLERANCE := 6.0

var _resizing_col := -1
var _drag_start_x := 0.0
var _drag_start_width := 0.0


func _ready() -> void:
	gui_input.connect(_on_gui_input)


func _on_gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			var col := _separator_at(event.position)
			if col >= 0:
				_resizing_col = col
				_drag_start_x = event.position.x
				_drag_start_width = float(get_column_width(col))
				accept_event()
				return
			# Pressed elsewhere (on an item / header title): never leave a stale drag.
			_resizing_col = -1
		elif _resizing_col >= 0:
			_resizing_col = -1
			accept_event()
			return
	elif event is InputEventMouseMotion:
		if _resizing_col >= 0:
			var delta: float = event.position.x - _drag_start_x
			var new_width: int = int(max(_drag_start_width + delta, 24.0))
			set_column_custom_minimum_width(_resizing_col, new_width)
			accept_event()
			return
		var over := _separator_at(event.position) >= 0
		var want := Control.CURSOR_HSIZE if over else Control.CURSOR_ARROW
		if mouse_default_cursor_shape != want:
			mouse_default_cursor_shape = want


## Column index whose right-hand separator is under `pos`, or -1 when the cursor
## is not near a column boundary. Deliberately ignores `pos.y` so the hit zone
## spans the whole tree height (header + every row), not just the header row.
func _separator_at(pos: Vector2) -> int:
	var x := 0.0
	for col in columns - 1:
		x += float(get_column_width(col))
		if absf(pos.x - x) <= SEPARATOR_TOLERANCE:
			return col
	return -1


## Header row height, mirroring Tree::get_header_height() (font height +
## v_separation gutters + sort-arrow gutter). Public helper kept so a regression
## test can target the header row.
func _header_height() -> float:
	var font := get_theme_font("font")
	var h := float(font.get_height(get_theme_font_size("font_size")))
	h += float(get_theme_constant("v_separation")) * 2.0
	var arrow := get_theme_icon("arrow", "Tree")
	if arrow != null:
		h = maxf(h, float(arrow.get_height()) + float(get_theme_constant("v_separation")) * 2.0)
	return h + 2.0
