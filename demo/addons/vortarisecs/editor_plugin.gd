@tool
extends EditorPlugin

# VortarisECS 的 ECS 运行时由 vortarisecs.gdextension 加载，与编辑器插件面板
# 无关。此 EditorPlugin 让插件出现在 Project > Plugins 中（便于启用/管理），
# 并挂载两个可选的调试界面：
#   * Inspector Dock（editor/ecs_inspector_dock.*）— 浏览编辑器进程内的世界
#     （因进程隔离，运行时游戏的世界它看不到）。**默认关闭**（V1）——需要时
#     通过 Project > Tools > "VortarisECS Inspector Dock" 手动调出。
#   * Debugger "ECS" Tab（editor/ecs_debugger_plugin.gd）— 远程监控运行中的
#     游戏世界（原理同 Godot 场景树的 Remote 模式）。
#
# The actual ECS runtime is loaded from vortarisecs.gdextension (Godot scans and
# loads it automatically). This EditorPlugin makes the add-on show up in
# Project > Plugins and attaches an optional inspector dock plus the remote
# debugger tab.

const DebuggerPluginScript := preload("res://addons/vortarisecs/editor/ecs_debugger_plugin.gd")
const InspectorDockScene := preload("res://addons/vortarisecs/editor/ecs_inspector_dock.tscn")

## Project > Tools entry that toggles the right-side inspector dock (V1). The
## dock is NOT added at startup; the user opts in here when they need it.
const DOCK_TOGGLE_ITEM := "VortarisECS Inspector Dock"

var _dock: Control = null
var _debugger_plugin: EditorDebuggerPlugin = null
var _dock_visible := false


func _enter_tree() -> void:
	# Remote monitor: adds an "ECS" tab to the debugger bottom panel while a
	# game runs, showing the running world's entities/components/systems/stats.
	_debugger_plugin = DebuggerPluginScript.new()
	add_debugger_plugin(_debugger_plugin)

	# V1: the inspector dock stays hidden by default. Project > Tools lets the
	# user bring it up (and hide it again) for the rest of the session.
	add_tool_menu_item(DOCK_TOGGLE_ITEM, _toggle_inspector_dock)


func _toggle_inspector_dock() -> void:
	if _dock_visible:
		_hide_inspector_dock()
	else:
		_show_inspector_dock()


func _show_inspector_dock() -> void:
	if _dock != null:
		return
	_dock = InspectorDockScene.instantiate()
	add_control_to_dock(DOCK_SLOT_RIGHT_UL, _dock)
	_dock_visible = true


func _hide_inspector_dock() -> void:
	if _dock == null:
		_dock_visible = false
		return
	remove_control_from_docks(_dock)
	_dock.queue_free()
	_dock = null
	_dock_visible = false


func _exit_tree() -> void:
	remove_tool_menu_item(DOCK_TOGGLE_ITEM)
	_hide_inspector_dock()
	if _debugger_plugin:
		remove_debugger_plugin(_debugger_plugin)
		_debugger_plugin = null
