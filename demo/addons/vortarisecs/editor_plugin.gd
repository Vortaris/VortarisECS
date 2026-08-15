@tool
extends EditorPlugin

# VortarisECS 的 ECS 运行时由 vortarisecs.gdextension 加载，与编辑器插件面板
# 无关。此 EditorPlugin 让插件出现在 Project > Plugins 中（便于启用/管理），
# 并挂载一个可选的 Inspector Dock 用于浏览世界。
#
# The actual ECS runtime is loaded from vortarisecs.gdextension (Godot scans and
# loads it automatically). This EditorPlugin makes the add-on show up in
# Project > Plugins and attaches an optional inspector dock.

var _dock: Control = null


func _enter_tree() -> void:
	# The dock is an editor convenience; never touch the world unless the VECS
	# engine singleton is actually present (headless / partial loads are safe).
	if Engine.has_singleton("VECS"):
		_dock = preload("res://addons/vortarisecs/editor/ecs_inspector_dock.tscn").instantiate()
		add_control_to_dock(DOCK_SLOT_RIGHT_UL, _dock)


func _exit_tree() -> void:
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
