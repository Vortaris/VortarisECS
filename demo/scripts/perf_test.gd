extends SceneTree

# Headless performance / stress test.
# Run with: godot --headless --path demo --script res://scripts/perf_test.gd

func _initialize() -> void:
	var world: VECSWorld = VECS.get_world()
	print("=== VortarisECS Performance Test ===")

	const N := 100000

	# --- create N entities (GDScript API path; includes script-bound overhead) ---
	var t0 := Time.get_ticks_usec()
	for i in N:
		var e: VECSEntity = world.create_entity()
		e.add_component("Position", {"x": 0.0, "y": 0.0, "z": 0.0})
		e.add_component("Velocity", {"x": 1.0, "y": 0.0, "z": 0.0})
	var t1 := Time.get_ticks_usec()
	print("create %d entities (script API): %.2f ms" % [N, (t1 - t0) / 1000.0])

	# --- C++ for_each<Position, Velocity> hot path via a system ---
	var ms: MoveSystem = MoveSystem.new()
	ms.group = "physics"
	world.add_system(ms)
	var t2 := Time.get_ticks_usec()
	world.process(0.016, "physics")
	var t3 := Time.get_ticks_usec()
	print("MoveSystem for_each over %d: %.3f ms (processed=%d)" % [N, (t3 - t2) / 1000.0, ms.get_processed_count()])

	# --- query count ---
	var t4 := Time.get_ticks_usec()
	var hits: int = world.query().with_all(["Position", "Velocity"]).count()
	var t5 := Time.get_ticks_usec()
	print("query count: %.3f ms (n=%d)" % [(t5 - t4) / 1000.0, hits])

	# --- deterministic snapshot serialization ---
	var t6 := Time.get_ticks_usec()
	var bytes: PackedByteArray = world.serialize_snapshot()
	var t7 := Time.get_ticks_usec()
	print("snapshot serialize: %.3f ms, %d bytes" % [(t7 - t6) / 1000.0, bytes.size()])

	var t8 := Time.get_ticks_usec()
	var fresh: VECSWorld = VECSWorld.new()
	var ok: bool = fresh.deserialize_snapshot(bytes)
	var t9 := Time.get_ticks_usec()
	print("snapshot deserialize: %.3f ms (ok=%s)" % [(t9 - t8) / 1000.0, str(ok)])
	fresh.free()

	# --- structural stress: random add/remove on a subset ---
	var t10 := Time.get_ticks_usec()
	var sub: Array = world.query().with_all(["Position", "Velocity"]).execute()
	var rng := RandomNumberGenerator.new()
	rng.seed = 1234
	var cmd: VECSCommandBuffer = world.commands()
	for i in 10000:
		var idx: int = rng.randi_range(0, sub.size() - 1)
		var ent: VECSEntity = sub[idx]
		if rng.randf() < 0.5:
			cmd.remove_component(ent, "Velocity")
		else:
			cmd.add_component(ent, "Velocity", {"x": 1.0, "y": 0.0, "z": 0.0})
	cmd.flush()
	var t11 := Time.get_ticks_usec()
	print("10000 structural changes via command buffer: %.3f ms" % [(t11 - t10) / 1000.0])
	print("final entity count: ", world.entity_count())

	# --- ChangeView::take() perf (issue#1 two-layer optimization) ---
	# First take reports every pre-existing Position entity; the steady-state take
	# should skip all unchanged archetypes via the per-column max-version fast path.
	var vp: ViewSystem = ViewSystem.new()
	vp.group = "changeview"
	world.add_system(vp)
	world.process(0.016, "changeview")  # first take: all pre-existing reported
	var first_changed: int = vp.get_changed_count()
	var t12 := Time.get_ticks_usec()
	world.process(0.016, "changeview")  # steady state, nothing changed
	var t13 := Time.get_ticks_usec()
	print("ChangeView take() steady-state over %d: %.3f ms (changed=%d)" % [N, (t13 - t12) / 1000.0, vp.get_changed_count()])
	var one_ent: VECSEntity = world.query().with_all(["Position"]).execute_one()
	one_ent.get_component("Position").set_field("x", 0.001)
	var t14 := Time.get_ticks_usec()
	world.process(0.016, "changeview")
	var t15 := Time.get_ticks_usec()
	print("ChangeView take() 1-change over %d: %.3f ms (changed=%d, first_pass=%d)" % [N, (t15 - t14) / 1000.0, vp.get_changed_count(), first_changed])
	world.remove_system(vp)
	vp.free()

	world.remove_system(ms)
	ms.free()
	print("=== VortarisECS Performance Test OK ===")
	quit(0)
