@tool
extends EditorPlugin

# VortarisECS 的 ECS 运行时由 vortarisecs.gdextension 加载，与编辑器插件面板
# 无关。此 EditorPlugin 仅让插件出现在 Project > Plugins 中，便于启用/管理。
#
# The actual ECS runtime is loaded from vortarisecs.gdextension (Godot scans and
# loads it automatically). This EditorPlugin exists so the add-on shows up in
# Project > Plugins and can be toggled there.
