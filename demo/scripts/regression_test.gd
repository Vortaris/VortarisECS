extends SceneTree

# Regression tests for VortarisECS. Run with:
#   godot --headless --path demo --script res://scripts/regression_test.gd
#
# Exit code 0 = all pass, 1 = failures. The suite grows with each fix phase;
# suites here cover the Phase-1 correctness fixes:
#   T1  apply_delta must consume a dead entity's component bytes (byte-stream alignment)
#   T2  deferred [add, remove] on the same component must net correctly
#   T3  execute_one() must honour the changed() filter
#   T4  changed() baselines must not bleed between queries with different filters
#   T5  deserialize replaces the existing world (JSON + binary)
#   T6  full_state is idempotent over an already-present entity id
#   T7  oversized ids / ranges are rejected instead of corrupting memory
#   T8  StringFixed truncation stays on a UTF-8 code-point boundary

const EcsTestUtil := preload("res://scripts/ecs_test_util.gd")

func _initialize() -> void:
	print("=== VortarisECS Regression Tests ===")
	var t: RefCounted = EcsTestUtil.new()
	_test_t1_delta_dead_entity(t)
	_test_t2_deferred_add_remove(t)
	_test_t3_execute_one_changed(t)
	_test_t4_changed_baseline_independent(t)
	_test_t5_deserialize_replaces(t)
	_test_t6_full_state_idempotent(t)
	_test_t7_input_validation(t)
	_test_t8_utf8_boundary(t)
	print("total=", t.total, " failures=", t.failures)
	if t.failures == 0:
		print("=== VortarisECS Regression OK ===")
		quit(0)
	else:
		printerr("=== VortarisECS Regression FAILED ===")
		quit(1)


# T1: server marks every entity dirty and sends a delta; the client has
# destroyed half of them locally. The delta for a dead entity must still be
# byte-consumed so the survivors deserialize correctly.
func _test_t1_delta_dead_entity(t: RefCounted) -> void:
	print("-- T1: delta targeting dead client entities --")
	var server_world: VECSWorld = VECSWorld.new()
	server_world.register_component("NetP", [{"name": "x", "type": "F32"}])
	# The component registry is global; the client world reuses the schema by name.
	var client_world: VECSWorld = VECSWorld.new()
	var server_ns: VECSNetworkSync = VECSNetworkSync.new()
	var client_ns: VECSNetworkSync = VECSNetworkSync.new()
	server_ns.set_server(true)
	server_ns.bind_world(server_world)
	client_ns.bind_world(client_world)
	server_ns.set_direct_peer(client_ns)

	# Server: 50 entities with NetP.x = index.
	var ids := {}
	for i in 50:
		var e: VECSEntity = server_world.create_entity()
		e.add_component("NetP", {"x": float(i)})
		ids[e.get_id()] = i
	server_ns.tick(0.016)  # spawn all 50 to the client

	# Client: destroy every even-id entity.
	var client_alive := {}
	var all_client: Array = client_world.query().with_all(["NetP"]).execute()
	for ent in all_client:
		if ent.get_id() % 2 == 0:
			client_world.destroy_entity(ent)
		else:
			client_alive[ent.get_id()] = ent
	t.expect_eq(client_alive.size(), 25, "T1: client kept 25 entities")

	# Server: dirty ALL 50 (value = index + 100) and send a delta.
	var server_ents: Array = server_world.query().with_all(["NetP"]).execute()
	for ent in server_ents:
		var comp: VECSComponent = ent.get_component("NetP")
		comp.set_field("x", comp.get_field("x") + 100.0)
	server_ns.tick(0.016)

	# Every surviving client entity must now read index + 100.
	var wrong := 0
	for id in client_alive:
		var got: float = client_alive[id].get_component("NetP").get_field("x")
		var expected: float = float(ids[id] + 100)
		if absf(got - expected) > 0.001:
			wrong += 1
			printerr("    id=", id, " got=", got, " want=", expected)
	t.expect_eq(wrong, 0, "T1: survivors deserialize correctly (" + str(wrong) + " wrong)")

	server_ns.free()
	client_ns.free()
	client_world.free()
	server_world.free()


# T2: deferred ops on the same component must net correctly.
func _test_t2_deferred_add_remove(t: RefCounted) -> void:
	print("-- T2: deferred add+remove net semantics --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("A", [{"name": "v", "type": "I32"}])

	# Scenario A: entity without A; [add A, remove A] -> still no A.
	var e1: VECSEntity = w.create_entity()
	var cmd1: VECSCommandBuffer = w.commands()
	cmd1.add_component(e1, "A", {"v": 1})
	cmd1.remove_component(e1, "A")
	cmd1.flush()
	t.expect_eq(e1.has_component("A"), false, "T2A: [add A, remove A] leaves no A")

	# Scenario B: entity with A; [add A(v2), remove A] -> A removed.
	var e2: VECSEntity = w.create_entity()
	e2.add_component("A", {"v": 0})
	var cmd2: VECSCommandBuffer = w.commands()
	cmd2.add_component(e2, "A", {"v": 2})
	cmd2.remove_component(e2, "A")
	cmd2.flush()
	t.expect_eq(e2.has_component("A"), false, "T2B: [add A(v2), remove A] removes A")

	# Scenario C: [remove A, add A(v)] -> A kept, value = last add.
	var e3: VECSEntity = w.create_entity()
	e3.add_component("A", {"v": 5})
	var cmd3: VECSCommandBuffer = w.commands()
	cmd3.remove_component(e3, "A")
	cmd3.add_component(e3, "A", {"v": 7})
	cmd3.flush()
	t.expect_eq(e3.has_component("A"), true, "T2C: [remove A, add A] keeps A")
	t.expect_eq(int(e3.get_component("A").get_field("v")), 7, "T2C: value = last add")

	w.free()


# T3: execute_one() must honour changed().
func _test_t3_execute_one_changed(t: RefCounted) -> void:
	print("-- T3: execute_one() honours changed() --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("B", [{"name": "v", "type": "F32"}])
	var e: VECSEntity = w.create_entity()
	e.add_component("B", {"v": 1.0})

	# First pass establishes the baseline and reports the entity.
	var q1: VECSQueryBuilder = w.query().with_all(["B"]).changed(["B"])
	var first: Array = q1.execute()
	t.expect(first.size() >= 1, "T3: initial changed() pass reports the entity")

	# No write since: execute_one must return nothing.
	var q2: VECSQueryBuilder = w.query().with_all(["B"]).changed(["B"])
	var one: VECSEntity = q2.execute_one()
	t.expect_eq(one, null, "T3: execute_one() is null when nothing changed")

	# Write, then execute_one returns an entity.
	var e2: VECSEntity = w.create_entity()
	e2.add_component("B", {"v": 2.0})
	var q3: VECSQueryBuilder = w.query().with_all(["B"]).changed(["B"])
	var one2: VECSEntity = q3.execute_one()
	t.expect(one2 != null, "T3: execute_one() returns entity after a write")

	w.free()


# T4: changed() baselines must not bleed between queries that share a
# membership but use different changed() filters.
func _test_t4_changed_baseline_independent(t: RefCounted) -> void:
	print("-- T4: changed() baselines are filter-independent --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("P", [{"name": "x", "type": "F32"}])
	w.register_component("V", [{"name": "x", "type": "F32"}])
	var a: VECSEntity = w.create_entity()
	a.add_component("P", {"x": 0.0})
	a.add_component("V", {"x": 0.0})

	var q1: VECSQueryBuilder = w.query().with_all(["P"]).changed(["P"])
	var r1: Array = q1.execute()  # baseline(Q1) = now; reports a
	t.expect(r1.size() >= 1, "T4: Q1 first pass reports the entity")

	# Change P then V (only P is watched by Q1; only V by Q2).
	a.get_component("P").set_field("x", 1.0)
	a.get_component("V").set_field("x", 1.0)

	# Q2 runs: must not advance Q1's baseline.
	var q2: VECSQueryBuilder = w.query().with_all(["P"]).changed(["V"])
	var r2: Array = q2.execute()
	t.expect(r2.size() >= 1, "T4: Q2 (V changed) reports the entity")

	# Q1 again: P changed after Q1's baseline, so it must still be reported.
	var q1b: VECSQueryBuilder = w.query().with_all(["P"]).changed(["P"])
	var r3: Array = q1b.execute()
	t.expect(r3.size() >= 1, "T4: Q1 still sees the earlier P change")

	w.free()


# T5: loading a save must replace the world, not stack on top of it.
func _test_t5_deserialize_replaces(t: RefCounted) -> void:
	print("-- T5: deserialize replaces existing entities --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("C5", [{"name": "v", "type": "I32"}])
	for i in 3:
		var e: VECSEntity = w.create_entity()
		e.add_component("C5", {"v": i})
	var save: Dictionary = w.serialize_snapshot_json()

	var fresh: VECSWorld = VECSWorld.new()
	for i in 5:
		var e: VECSEntity = fresh.create_entity()
		e.add_component("C5", {"v": 100 + i})
	t.expect_eq(fresh.entity_count(), 5, "T5: pre-load entity count")
	var ok: bool = fresh.deserialize_snapshot_json(save)
	t.expect_eq(ok, true, "T5: JSON load ok")
	t.expect_eq(fresh.entity_count(), 3, "T5: JSON load replaced the world")

	var fresh2: VECSWorld = VECSWorld.new()
	var stray: VECSEntity = fresh2.create_entity()
	stray.add_component("C5", {"v": 999})
	var bytes: PackedByteArray = w.serialize_snapshot()
	t.expect_eq(fresh2.deserialize_snapshot(bytes), true, "T5: binary load ok")
	t.expect_eq(fresh2.entity_count(), 3, "T5: binary load replaced the world")

	w.free()
	fresh.free()
	fresh2.free()


# T6: full_state applied over an already-present id must overwrite, not abort.
func _test_t6_full_state_idempotent(t: RefCounted) -> void:
	print("-- T6: full_state idempotent overwrite --")
	var sw: VECSWorld = VECSWorld.new()
	sw.register_component("NetF", [{"name": "v", "type": "F32"}])
	var cw: VECSWorld = VECSWorld.new()
	var s_ns: VECSNetworkSync = VECSNetworkSync.new()
	var c_ns: VECSNetworkSync = VECSNetworkSync.new()
	s_ns.set_server(true)
	s_ns.bind_world(sw)
	c_ns.bind_world(cw)
	s_ns.set_direct_peer(c_ns)

	var se: VECSEntity = sw.create_entity()
	se.add_component("NetF", {"v": 10.0})
	s_ns.tick(0.016)  # spawn -> client has the entity with v=10

	var cq: Array = cw.query().with_all(["NetF"]).execute()
	t.expect_eq(cq.size(), 1, "T6: client got the spawn")
	var ce: VECSEntity = cq[0]
	ce.get_component("NetF").set_field("v", 42.0)  # local divergence

	se.get_component("NetF").set_field("v", 20.0)  # server authoritative value
	s_ns.request_full_state()
	t.expect_eq(float(ce.get_component("NetF").get_field("v")), 20.0, "T6: full_state overwrote client value")

	s_ns.free()
	c_ns.free()
	cw.free()
	sw.free()


# T7: oversized preassigned ids and ranges are rejected without corrupting memory.
func _test_t7_input_validation(t: RefCounted) -> void:
	print("-- T7: input validation --")
	var w: VECSWorld = VECSWorld.new()
	w.set_entity_range(-1)        # rejected (ERR_PRINT), must not crash
	w.set_entity_range(1 << 30)   # rejected, must not allocate 1B slots
	var bad: VECSEntity = w.create_entity_preassigned((1 << 24) + 1)  # slot past the cap
	t.expect_eq(bad, null, "T7: oversized preassigned id rejected")
	var e: VECSEntity = w.create_entity()
	t.expect(e.is_alive(), "T7: normal create still works")
	w.free()


# T8: StringFixed truncation must stay on a UTF-8 code-point boundary.
func _test_t8_utf8_boundary(t: RefCounted) -> void:
	print("-- T8: StringFixed UTF-8 boundary --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("Str", [{"name": "s", "type": "StringFixed", "count": 8}])
	var e: VECSEntity = w.create_entity()
	# "中中中" = 9 bytes, truncated to 7 content bytes. A naive cut lands inside
	# the third "中"; the fix backs off so only two full characters are stored.
	e.add_component("Str", {"s": "中中中"})
	var s: String = e.get_component("Str").get_field("s")
	t.expect_eq(s.length(), 2, "T8: truncated string has 2 full chars (got %d)" % s.length())

	var e2: VECSEntity = w.create_entity()
	e2.add_component("Str", {"s": "中文"})  # 6 bytes, fits exactly
	t.expect_eq(String(e2.get_component("Str").get_field("s")), "中文", "T8: exact fit round-trips")
	w.free()
