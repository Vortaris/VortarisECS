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
#   T9  distinct component sets stay distinct (archetype keying)
#   T11 change tracking first pass reports pre-existing writes
#   T12 schema-only layout matches the real Godot type sizes
#   T14 convenience API (spawn / each / getf / setf)
# 0.2.0 additions:
#   T15 entity(id)/has_entity(id) lookup + get_component null for unattached
#   T16 StringFixed truncation keeps whole chars, warns, count==0 safe
#   T18 observer field-level filter + change-tick throttle
#   T19 network packet validation rejects truncated/id-conflict packets
#   T20 shutdown resets transient state and the world stays reusable

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
	_test_t9_archetype_sets(t)
	_test_t11_change_tracking_init(t)
	_test_t12_schema_size(t)
	_test_t14_convenience_api(t)
	_test_t15_entity_lookup(t)
	_test_t16_stringfixed_warning(t)
	_test_t18_observer_field_throttle(t)
	_test_t19_network_validation(t)
	_test_t20_shutdown(t)
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


# T9: distinct component sets must live in distinct archetypes.
func _test_t9_archetype_sets(t: RefCounted) -> void:
	print("-- T9: distinct component sets stay distinct --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T9A", [{"name": "v", "type": "I32"}])
	w.register_component("T9B", [{"name": "v", "type": "I32"}])
	w.register_component("T9C", [{"name": "v", "type": "I32"}])
	w.register_component("T9D", [{"name": "v", "type": "I32"}])
	var combos := [
		["T9A"],
		["T9A", "T9B"],
		["T9B"],
		["T9A", "T9B", "T9C"],
		["T9D"],
	]
	for comps in combos:
		var e: VECSEntity = w.create_entity()
		for cname in comps:
			e.add_component(cname, {"v": 1})
	t.expect_eq(w.query().with_all(["T9A", "T9B"]).execute().size(), 2, "T9: A+B matches exactly 2")
	t.expect_eq(w.query().with_all(["T9A", "T9B", "T9C"]).execute().size(), 1, "T9: A+B+C matches exactly 1")
	t.expect_eq(w.query().with_all(["T9D"]).execute().size(), 1, "T9: D matches exactly 1")
	t.expect_eq(w.query().with_all(["T9A"]).execute().size(), 3, "T9: A present in 3")
	w.free()


# T11: change tracking enabled after writes must report those rows once.
func _test_t11_change_tracking_init(t: RefCounted) -> void:
	print("-- T11: change tracking first pass reports pre-existing writes --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T11", [{"name": "v", "type": "F32"}])
	var e: VECSEntity = w.create_entity()
	e.add_component("T11", {"v": 1.0})  # write before any changed() query
	var q1: VECSQueryBuilder = w.query().with_all(["T11"]).changed(["T11"])
	var r1: Array = q1.execute()
	t.expect(r1.size() >= 1, "T11: first pass reports pre-enabled writes")
	var q2: VECSQueryBuilder = w.query().with_all(["T11"]).changed(["T11"])
	var r2: Array = q2.execute()
	t.expect_eq(r2.size(), 0, "T11: second pass with no writes is empty")
	w.free()


# T12: schema-only layout must match the real Godot type sizes (float build).
func _test_t12_schema_size(t: RefCounted) -> void:
	print("-- T12: schema layout matches real Godot types --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("Vec3", [{"name": "p", "type": "Vector3"}, {"name": "t", "type": "Transform3D"}])
	var ct: VECSComponentType = w.get_component_type("Vec3")
	# float build: Vector3=12 (align 4), Transform3D=48 (Basis 36 + origin 12).
	# The old hardcoded 64 for Transform3D was wrong; sizeof() gives 48.
	t.expect_eq(int(ct.get_size()), 60, "T12: Vector3+Transform3D schema size")
	w.free()


# T14: convenience sugar — spawn(), each(), getf/setf, world-level field access.
func _test_t14_convenience_api(t: RefCounted) -> void:
	print("-- T14: convenience API --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("C14", [{"name": "x", "type": "F32"}, {"name": "y", "type": "F32"}])
	w.register_component("C14b", [{"name": "v", "type": "F32"}])
	var e: VECSEntity = w.spawn({"C14": {"x": 1.0, "y": 2.0}, "C14b": {"v": 0.5}})
	t.expect(e != null, "T14: spawn returns an entity")
	t.expect_eq(float(e.getf("C14", "x")), 1.0, "T14: getf reads a field")
	e.setf("C14", "y", 9.0)
	t.expect_eq(float(e.getf("C14", "y")), 9.0, "T14: setf writes a field")

	var seen := []
	w.each(["C14"], func(ent: VECSEntity) -> void:
		seen.append(ent))  # GDScript lambdas capture primitives by value
	t.expect_eq(seen.size(), 1, "T14: each visits the matching entity")

	t.expect_eq(float(w.get_field(e, "C14", "x")), 1.0, "T14: world.get_field")
	w.set_field(e, "C14", "x", 42.0)
	t.expect_eq(float(e.getf("C14", "x")), 42.0, "T14: world.set_field")
	w.free()


# T15: get_component on an unattached component returns null; entity(id) /
# has_entity(id) resolve live vs dead handles.
func _test_t15_entity_lookup(t: RefCounted) -> void:
	print("-- T15: entity lookup --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T15C", [{"name": "v", "type": "I32"}])
	var e: VECSEntity = w.create_entity()
	var unattached: VECSComponent = e.get_component("T15C")
	t.expect_eq(unattached, null, "T15: get_component null when component not attached")
	e.add_component("T15C", {"v": 5})
	var attached: VECSComponent = e.get_component("T15C")
	t.expect(attached != null, "T15: get_component wrapper once attached")

	var eid: int = e.get_id()
	var found: VECSEntity = w.entity(eid)
	t.expect(found != null, "T15: entity(id) finds a live entity")
	t.expect_eq(found.get_id(), eid, "T15: entity(id) returns the same id")
	t.expect_eq(w.has_entity(eid), true, "T15: has_entity(id) true for live entity")
	t.expect_eq(w.entity(0), null, "T15: entity(0) is null")
	t.expect_eq(w.has_entity(0), false, "T15: has_entity(0) false")
	w.destroy_entity(e)
	t.expect_eq(w.entity(eid), null, "T15: entity(id) null after destroy")
	t.expect_eq(w.has_entity(eid), false, "T15: has_entity(id) false after destroy")
	w.free()


# T16: StringFixed truncation keeps whole UTF-8 characters, emits a warning, and
# a count==0 buffer stores an empty string without crashing.
func _test_t16_stringfixed_warning(t: RefCounted) -> void:
	print("-- T16: StringFixed truncation + warning --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("Str16", [{"name": "s", "type": "StringFixed", "count": 8}])
	var e: VECSEntity = w.create_entity()
	# "中中中中" = 12 UTF-8 bytes; the 8-byte buffer holds 7 content bytes, which
	# truncates to exactly 2 whole characters (6 bytes).
	e.add_component("Str16", {"s": "中中中中"})
	var s: String = e.get_component("Str16").get_field("s")
	t.expect_eq(s.length(), 2, "T16: 4 chars truncated to 2 whole chars (got %d)" % s.length())

	w.register_component("Str0", [{"name": "s", "type": "StringFixed", "count": 0}])
	var e2: VECSEntity = w.create_entity()
	e2.add_component("Str0", {"s": "abc"})
	t.expect_eq(String(e2.get_component("Str0").get_field("s")), "", "T16: count==0 StringFixed stores empty string")
	w.free()


# T18: observer field-level subscription (write x triggers, write y does not)
# and change-tick throttling (rapid writes within the window are suppressed).
func _test_t18_observer_field_throttle(t: RefCounted) -> void:
	print("-- T18: observer field filter + throttle --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T18C", [{"name": "x", "type": "F32"}, {"name": "y", "type": "F32"}])

	var obs = preload("res://scripts/test_observer.gd").new()
	obs.on_changed()
	obs.set_fields(["x"])
	w.add_observer(obs)
	var e: VECSEntity = w.create_entity()
	e.add_component("T18C", {"x": 0.0, "y": 0.0})
	obs.event_log.clear()
	e.get_component("T18C").set_field("x", 1.0)
	t.expect_eq(obs.event_log.size(), 1, "T18: field x subscribed -> delivered")
	obs.event_log.clear()
	e.get_component("T18C").set_field("y", 1.0)
	t.expect_eq(obs.event_log.size(), 0, "T18: field y not subscribed -> suppressed")
	w.remove_observer(obs)
	obs.free()

	var obs2 = preload("res://scripts/test_observer.gd").new()
	obs2.on_changed()
	obs2.set_fields(["x"])
	obs2.set_throttle_tick(10)
	w.add_observer(obs2)
	var e2: VECSEntity = w.create_entity()
	e2.add_component("T18C", {"x": 0.0, "y": 0.0})
	obs2.event_log.clear()
	for i in 5:
		e2.get_component("T18C").set_field("x", float(i))
	t.expect_eq(obs2.event_log.size(), 1, "T18: throttle suppressed 4 of 5 rapid writes")
	obs2.event_log.clear()
	# Advance the change clock past the 10-tick throttle window, then write again.
	for i in 10:
		w.process(0.0, "")
	e2.get_component("T18C").set_field("x", 99.0)
	t.expect_eq(obs2.event_log.size(), 1, "T18: throttle expires after the window")
	w.remove_observer(obs2)
	obs2.free()
	w.free()


# T19: malformed network packets are rejected before any state is written; an
# id-conflict spawn is rejected without overwriting the existing entity.
func _test_t19_network_validation(t: RefCounted) -> void:
	print("-- T19: network packet validation --")
	var sw: VECSWorld = VECSWorld.new()
	sw.register_component("NetT19", [{"name": "v", "type": "F32"}])
	var cw: VECSWorld = VECSWorld.new()
	var s_ns: VECSNetworkSync = VECSNetworkSync.new()
	var c_ns: VECSNetworkSync = VECSNetworkSync.new()
	s_ns.set_server(true)
	s_ns.bind_world(sw)
	c_ns.bind_world(cw)
	s_ns.set_direct_peer(c_ns)

	var se: VECSEntity = sw.create_entity()
	se.add_component("NetT19", {"v": 10.0})
	s_ns.tick(0.016)  # baseline spawn reaches the client
	t.expect_eq(cw.query().with_all(["NetT19"]).execute().size(), 1, "T19: baseline spawn applied")

	var type_id: int = cw.get_component_type("NetT19").get_id()
	var sess: int = c_ns.get_session_id()

	# Truncated spawn: version | id | ncomp=1 | type_id | only 2 of 4 bytes.
	var pba := PackedByteArray()
	pba.resize(2 + 8 + 2 + 4 + 2)
	pba.encode_u16(0, 1)
	var fresh_id: int = 1000
	for i in 8:
		pba.encode_u8(2 + i, (fresh_id >> (i * 8)) & 0xFF)
	pba.encode_u16(10, 1)
	for i in 4:
		pba.encode_u8(12 + i, (type_id >> (i * 8)) & 0xFF)
	var before_count: int = cw.entity_count()
	c_ns._rpc_spawn(pba, sess)
	t.expect_eq(cw.entity_count(), before_count, "T19: truncated spawn dropped, no partial entity")

	# Well-formed spawn for an already-occupied id: rejected at preassigned id
	# (packet-level failure), the existing entity's value stays intact.
	var existing: VECSEntity = cw.query().with_all(["NetT19"]).execute()[0]
	var conflict_id: int = existing.get_id()
	var pba2 := PackedByteArray()
	pba2.resize(2 + 8 + 2 + 4 + 4)
	pba2.encode_u16(0, 1)
	for i in 8:
		pba2.encode_u8(2 + i, (conflict_id >> (i * 8)) & 0xFF)
	pba2.encode_u16(10, 1)
	for i in 4:
		pba2.encode_u8(12 + i, (type_id >> (i * 8)) & 0xFF)
	pba2.encode_float(16, 99.0)
	c_ns._rpc_spawn(pba2, sess)
	var after: VECSEntity = cw.query().with_all(["NetT19"]).execute()[0]
	t.expect_eq(float(after.get_component("NetT19").get_field("v")), 10.0, "T19: id-conflict spawn rejected, value unchanged")

	s_ns.free()
	c_ns.free()
	cw.free()
	sw.free()


# T20: shutdown() clears transient state (deferred ops, baselines) while
# preserving entities; the world remains usable afterwards.
func _test_t20_shutdown(t: RefCounted) -> void:
	print("-- T20: shutdown resets transient state --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T20C", [{"name": "v", "type": "I32"}])
	var e: VECSEntity = w.create_entity()
	e.add_component("T20C", {"v": 1})
	var e2: VECSEntity = w.create_entity()
	var cmd: VECSCommandBuffer = w.commands()
	cmd.add_component(e2, "T20C", {"v": 2})  # queued, never flushed
	w.query().with_all(["T20C"]).changed(["T20C"]).execute()  # establishes a baseline
	w.shutdown()
	t.expect_eq(w.entity_count(), 2, "T20: entities preserved across shutdown")
	var e3: VECSEntity = w.create_entity()
	e3.add_component("T20C", {"v": 3})
	t.expect_eq(w.entity_count(), 3, "T20: world reusable after shutdown")
	cmd.flush()  # buffer was cleared by shutdown; must be a no-op
	t.expect_eq(e2.has_component("T20C"), false, "T20: pending command buffer cleared by shutdown")
	w.free()
