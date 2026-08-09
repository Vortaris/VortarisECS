extends SceneTree

# Minimal end-to-end example — the "hello world" of VortarisECS.
# Run with:
#   godot --headless --path demo --script res://scripts/quickstart.gd
#
# Shows the convenience API: script-defined components, spawn(), each() with a
# lambda, getf()/setf() field access, queries and a JSON save.

func _initialize() -> void:
	var w: VECSWorld = VECS.get_world()

	# 1) Script-defined components — no C++ struct required.
	w.register_component("Pos", [{"name": "x", "type": "F32"}, {"name": "y", "type": "F32"}])
	w.register_component("Vel", [{"name": "x", "type": "F32"}])

	# 2) Spawn an entity in one call.
	var e := w.spawn({"Pos": {"x": 1.0, "y": 2.0}, "Vel": {"x": 0.5}})

	# 3) Iterate with a lambda — no Array is materialized.
	w.each(["Pos", "Vel"], func(ent: VECSEntity) -> void:
		ent.setf("Pos", "x", ent.getf("Pos", "x") + ent.getf("Vel", "x")))

	# 4) Read a field back.
	print("pos.x = ", e.getf("Pos", "x"))  # expect 1.5
	assert(e.getf("Pos", "x") == 1.5)

	# 5) Query + JSON save (deterministic order).
	print("entities with Pos = ", w.query().with_all(["Pos"]).count())
	var save_text := JSON.stringify(w.serialize_snapshot_json(), "\t")
	print("save length = ", save_text.length())

	print("=== VortarisECS Quickstart OK ===")
	quit(0)
