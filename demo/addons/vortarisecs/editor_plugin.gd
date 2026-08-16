@tool
extends EditorPlugin

# VortarisECS 的 ECS 运行时由 vortarisecs.gdextension 加载，与编辑器插件面板
# 无关。此 EditorPlugin 让插件出现在 Project > Plugins 中（便于启用/管理），
# 并挂载两个可选的调试界面：
#   * Inspector Dock（editor/ecs_inspector_dock.*）— 浏览编辑器进程内的世界
#     （因进程隔离，运行时游戏的世界它看不到）。
#   * Debugger "ECS" Tab（editor/ecs_debugger_plugin.gd）— 远程监控运行中的
#     游戏世界（原理同 Godot 场景树的 Remote 模式）。
#
# The actual ECS runtime is loaded from vortarisecs.gdextension (Godot scans and
# loads it automatically). This EditorPlugin makes the add-on show up in
# Project > Plugins and attaches an optional inspector dock plus the remote
# debugger tab.

const DebuggerPluginScript := preload("res://addons/vortarisecs/editor/ecs_debugger_plugin.gd")

var _dock: Control = null
var _debugger_plugin: EditorDebuggerPlugin = null


func _enter_tree() -> void:
	# The dock is an editor convenience; never touch the world unless the VECS
	# engine singleton is actually present (headless / partial loads are safe).
	if Engine.has_singleton("VECS"):
		_dock = preload("res://addons/vortarisecs/editor/ecs_inspector_dock.tscn").instantiate()
		add_control_to_dock(DOCK_SLOT_RIGHT_UL, _dock)

	# Remote monitor: adds an "ECS" tab to the debugger bottom panel while a
	# game runs, showing the running world's entities/components/systems/stats.
	_debugger_plugin = DebuggerPluginScript.new()
	add_debugger_plugin(_debugger_plugin)


func _exit_tree() -> void:
	if _dock:
		remove_control_from_docks(_dock)
		_dock.queue_free()
		_dock = null
	if _debugger_plugin:
		remove_debugger_plugin(_debugger_plugin)
		_debugger_plugin = null
