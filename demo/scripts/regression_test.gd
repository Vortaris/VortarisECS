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
#   T21 get_field / getf / component.get_field default value
#   T22 find_by_components convenience
#   T23 array-field element access (component + component type)
#   T24 where predicate + order_by / order_by_id
#   T25 spawn_from_data_mapped / deserialize_snapshot_json_mapped + remap_reference
#   T26 entity pooling (no generation bump)
#   T27 cross-world copy / merge (including self-merge)
#   T28 on_field_changed value-compared subscription + off
#   T29 event bus: subscribe_event / unsubscribe_event / emit return count
#   T30 get_debug_stats + query execution time
#   T31 ChangeView::take() log consistency (pre-existing once, dedup)
#   T32 sync_priority delta throttle (MEDIUM ~ 0.1s)
# 0.2.1 additions:
#   T33 verbose logging flag (set_verbose / is_verbose + project setting)
#   T34 failed JSON snapshot load restores the live world instead of clearing it

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
	_test_t21_field_default(t)
	_test_t22_find_by_components(t)
	_test_t23_array_element(t)
	_test_t24_where_order(t)
	_test_t25_id_mapping(t)
	_test_t26_pooling(t)
	_test_t27_merge(t)
	_test_t28_on_field_changed(t)
	_test_t29_event_bus(t)
	_test_t30_debug_stats(t)
	_test_t31_changeview_log(t)
	_test_t32_sync_throttle(t)
	_test_t33_verbose_flag(t)
	_test_t34_bad_snapshot_preserves_world(t)
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


# T21: get_field / getf / component.get_field accept a default returned when the
# component or field is missing.
func _test_t21_field_default(t: RefCounted) -> void:
	print("-- T21: field default values --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T21C", [{"name": "x", "type": "F32"}])
	var e: VECSEntity = w.create_entity()
	t.expect_eq(float(w.get_field(e, "T21C", "x", 42.0)), 42.0, "T21: world.get_field default for missing component")
	t.expect_eq(float(e.getf("T21C", "x", 7.0)), 7.0, "T21: entity.getf default for missing component")
	e.add_component("T21C", {"x": 1.0})
	t.expect_eq(float(e.getf("T21C", "x", 7.0)), 1.0, "T21: getf returns real value when present")
	t.expect_eq(float(e.get_component("T21C").get_field("missing", 3.5)), 3.5, "T21: component.get_field default for missing field")
	t.expect_eq(float(w.get_field(e, "T21C", "y", 99.0)), 99.0, "T21: world.get_field default for missing field")
	w.free()


# T22: find_by_components == query().with_all(comps).execute_one().
func _test_t22_find_by_components(t: RefCounted) -> void:
	print("-- T22: find_by_components --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T22A", [{"name": "v", "type": "I32"}])
	w.register_component("T22B", [{"name": "v", "type": "I32"}])
	w.register_component("T22C", [{"name": "v", "type": "I32"}])
	var e1: VECSEntity = w.create_entity()
	e1.add_component("T22A", {"v": 1})
	var e2: VECSEntity = w.create_entity()
	e2.add_component("T22A", {"v": 2})
	e2.add_component("T22B", {"v": 3})
	var found: VECSEntity = w.find_by_components(["T22A", "T22B"])
	t.expect(found != null, "T22: find_by_components finds a match")
	t.expect_eq(found.get_id(), e2.get_id(), "T22: returns the A+B entity")
	t.expect_eq(w.find_by_components(["T22A", "T22C"]), null, "T22: null when no match")
	w.free()


# T23: fixed-array field convenience — element read/write on the component and
# count/type metadata on the component type.
func _test_t23_array_element(t: RefCounted) -> void:
	print("-- T23: array field element access --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T23C", [{"name": "arr", "type": "F32", "count": 3}])
	var e: VECSEntity = w.create_entity()
	e.add_component("T23C", {"arr": [1.0, 2.0, 3.0]})
	var comp: VECSComponent = e.get_component("T23C")
	t.expect_eq(int(comp.get_field_count("arr")), 3, "T23: field count is 3 for array")
	t.expect_eq(float(comp.get_array_element("arr", 1)), 2.0, "T23: read element 1")
	t.expect_eq(comp.get_array_element("arr", 5), null, "T23: out-of-range read is null")
	var ok: bool = comp.set_array_element("arr", 2, 9.0)
	t.expect_eq(ok, true, "T23: set element ok")
	t.expect_eq(float(comp.get_array_element("arr", 2)), 9.0, "T23: element updated")
	t.expect_eq(comp.set_array_element("arr", 9, 1.0), false, "T23: out-of-range write false")
	var ct: VECSComponentType = w.get_component_type("T23C")
	t.expect_eq(int(ct.get_field_count("arr")), 3, "T23: type field count")
	t.expect_eq(String(ct.get_field_type("arr")), "Array:F32", "T23: array field type string")
	t.expect_eq(String(ct.get_field_type("missing")), "", "T23: unknown field type empty")
	w.free()


# T24: where() predicate filter (execute/count/execute_one) + order_by /
# order_by_id sorting of execute() results.
func _test_t24_where_order(t: RefCounted) -> void:
	print("-- T24: where + order_by --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T24C", [{"name": "rank", "type": "I32"}])
	for i in 5:
		var e: VECSEntity = w.create_entity()
		e.add_component("T24C", {"rank": 5 - i})  # ranks 5,4,3,2,1
	var filtered: Array = w.query().with_all(["T24C"]).where(func(ent: VECSEntity) -> bool:
		return int(ent.get_component("T24C").get_field("rank")) > 2).execute()
	t.expect_eq(filtered.size(), 3, "T24: where filters to rank>2")
	t.expect_eq(int(w.query().with_all(["T24C"]).where(func(ent: VECSEntity) -> bool:
		return int(ent.get_component("T24C").get_field("rank")) > 2).count()), 3, "T24: count applies where")
	var one: VECSEntity = w.query().with_all(["T24C"]).where(func(ent: VECSEntity) -> bool:
		return int(ent.get_component("T24C").get_field("rank")) == 1).execute_one()
	t.expect(one != null, "T24: execute_one applies where")
	t.expect_eq(int(one.get_component("T24C").get_field("rank")), 1, "T24: execute_one returns the match")
	var sorted: Array = w.query().with_all(["T24C"]).order_by("T24C", "rank").execute()
	var ranks := []
	for ent in sorted:
		ranks.append(int(ent.get_component("T24C").get_field("rank")))
	t.expect_eq(ranks, [1, 2, 3, 4, 5], "T24: order_by rank ascending")
	var sorted_id: Array = w.query().with_all(["T24C"]).order_by_id().execute()
	var prev: int = -1
	var ok_order: bool = true
	for ent in sorted_id:
		if int(ent.get_id()) < prev:
			ok_order = false
		prev = int(ent.get_id())
	t.expect_eq(ok_order, true, "T24: order_by_id ascending")
	w.free()


# T25: spawn_from_data_mapped / deserialize_snapshot_json_mapped id mappings and
# remap_reference rewriting of cross-entity references.
func _test_t25_id_mapping(t: RefCounted) -> void:
	print("-- T25: id mapping + remap_reference --")
	var w: VECSWorld = VECSWorld.new()
	w.register_components({
		"T25A": [{"name": "v", "type": "I32"}],
		"T25B": [{"name": "target", "type": "I64"}],
	})
	var mapping: Dictionary = w.spawn_from_data_mapped([
		{"id": 100, "components": {"T25A": {"v": 1}, "T25B": {"target": 200}}},
		{"id": 200, "components": {"T25A": {"v": 2}, "T25B": {"target": 100}}},
	])
	t.expect_eq(mapping.size(), 2, "T25: mapping has 2 entries")
	t.expect_eq(int(mapping[100]), 100, "T25: preassigned id preserved")
	t.expect_eq(int(mapping[200]), 200, "T25: second preassigned id preserved")
	w.remap_reference(w.entity(100), "T25B", "target", mapping)
	t.expect_eq(int(w.entity(100).get_component("T25B").get_field("target")), 200, "T25: remap no-op when ids preserved")

	# JSON round-trip: save has explicit ids, mapped load returns id->id.
	# (Component registry is process-global, so no re-registration is needed.)
	var save: Dictionary = w.serialize_snapshot_json()
	var w3: VECSWorld = VECSWorld.new()
	var m3: Dictionary = w3.deserialize_snapshot_json_mapped(save)
	t.expect_eq(m3.size(), 2, "T25: deserialize_mapped returns mapping")
	t.expect_eq(int(m3[100]), 100, "T25: json mapped id 100")
	w3.free()

	# Index-keyed spawn (no explicit id): map keys are array indices; remap
	# rewrites a stored index reference to the freshly assigned entity id.
	var w2: VECSWorld = VECSWorld.new()
	var m2: Dictionary = w2.spawn_from_data_mapped([
		{"components": {"T25A": {"v": 1}, "T25B": {"target": 0}}},
		{"components": {"T25A": {"v": 2}, "T25B": {"target": 1}}},
	])
	t.expect_eq(m2.size(), 2, "T25: index-keyed mapping has 2 entries")
	t.expect_eq(int(m2[0]), 1, "T25: index 0 maps to a new id")
	var all: Array = w2.query().with_all(["T25A"]).execute()
	var first: VECSEntity = all[0]
	var second: VECSEntity = all[1]
	w2.remap_reference(first, "T25B", "target", m2)
	w2.remap_reference(second, "T25B", "target", m2)
	t.expect_eq(int(first.get_component("T25B").get_field("target")), int(m2[0]), "T25: index 0 remapped to first new id")
	t.expect_eq(int(second.get_component("T25B").get_field("target")), int(m2[1]), "T25: index 1 remapped to second new id")
	w2.free()

	# Auto-parenting: an entry with a "parent" key has its "parent" field
	# rewritten to the freshly assigned parent id after the batch.
	var wp: VECSWorld = VECSWorld.new()
	wp.register_component("T25P", [{"name": "parent", "type": "I64"}])
	var mp2: Dictionary = wp.spawn_from_data_mapped([
		{"components": {"T25P": {"parent": 0}}},           # parent, index 0
		{"parent": 0, "components": {"T25P": {"parent": 0}}},  # child, references parent by index
	])
	var parents: Array = wp.query().with_all(["T25P"]).execute()
	var child: VECSEntity = parents[1]
	t.expect_eq(int(child.get_component("T25P").get_field("parent")), int(mp2[0]), "T25: auto-parent rewrote child reference")
	wp.free()
	w.free()


# T26: pooled entities recycle ids without bumping generation; pooled slots are
# never handed out by the regular allocator.
func _test_t26_pooling(t: RefCounted) -> void:
	print("-- T26: entity pooling --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T26C", [{"name": "v", "type": "I32"}])
	var e: VECSEntity = w.create_entity_pooled()
	e.add_component("T26C", {"v": 5})
	var eid: int = e.get_id()
	w.destroy_entity_pooled(e)
	t.expect_eq(int(w.pool_size()), 1, "T26: pooled destroy reclaims the id")
	t.expect_eq(w.entity(eid), null, "T26: entity dead after pooled destroy")
	var e2: VECSEntity = w.create_entity_pooled()
	t.expect_eq(e2.get_id(), eid, "T26: pooled create reuses the same id")
	t.expect_eq(int(w.pool_size()), 0, "T26: pool empty after reuse")
	t.expect_eq(e.is_alive(), true, "T26: stale handle valid after pooled reuse (no generation bump)")
	t.expect(e2.has_component("T26C") == false, "T26: pooled entity starts with no components")
	w.destroy_entity_pooled(e2)
	var e3: VECSEntity = w.create_entity()
	t.expect(e3.get_id() != eid, "T26: create_entity does not reuse pooled slot")
	w.free()


# T27: copy_entity_to / merge_world across worlds, and self-merge (clone).
func _test_t27_merge(t: RefCounted) -> void:
	print("-- T27: cross-world copy/merge --")
	var src: VECSWorld = VECSWorld.new()
	src.register_component("T27C", [{"name": "v", "type": "F32"}])
	var se: VECSEntity = src.create_entity()
	se.add_component("T27C", {"v": 7.0})
	var dst: VECSWorld = VECSWorld.new()
	var m: Dictionary = src.copy_entity_to(se, dst)
	t.expect_eq(m.size(), 1, "T27: copy returns a mapping")
	t.expect(int(m[se.get_id()]) > 0, "T27: target id positive")
	var dq: Array = dst.query().with_all(["T27C"]).execute()
	t.expect_eq(dq.size(), 1, "T27: target has the copied entity")
	t.expect_eq(float(dq[0].get_component("T27C").get_field("v")), 7.0, "T27: copied field value")
	var dst2: VECSWorld = VECSWorld.new()
	var total: Dictionary = dst2.merge_world(src)
	t.expect_eq(total.size(), 1, "T27: merge maps the source entity")
	t.expect_eq(int(dst2.entity_count()), 1, "T27: merge copied the entity")
	var total_self: Dictionary = src.merge_world(src)
	t.expect_eq(total_self.size(), 1, "T27: self-merge maps the source")
	t.expect_eq(int(src.entity_count()), 2, "T27: self-merge clones the entity")
	src.free()
	dst.free()
	dst2.free()


# T28: on_field_changed compares values before firing; off() unsubscribes.
func _test_t28_on_field_changed(t: RefCounted) -> void:
	print("-- T28: on_field_changed value subscription --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T28C", [{"name": "x", "type": "F32"}])
	var events := []
	var sid: int = w.on_field_changed("T28C", "x", func(ent: VECSEntity, value: Variant) -> void:
		events.append([ent.get_id(), value]))
	t.expect(sid > 0, "T28: subscription id returned")
	var e: VECSEntity = w.create_entity()
	e.add_component("T28C", {"x": 0.0})
	t.expect_eq(events.size(), 0, "T28: add_component does not fire field change")
	e.get_component("T28C").set_field("x", 1.0)
	t.expect_eq(events.size(), 1, "T28: real value change fires")
	t.expect_eq(float(events[0][1]), 1.0, "T28: callback receives the new value")
	e.get_component("T28C").set_field("x", 1.0)
	t.expect_eq(events.size(), 1, "T28: same value does not fire again")
	w.off(sid)
	e.get_component("T28C").set_field("x", 2.0)
	t.expect_eq(events.size(), 1, "T28: off() unsubscribes")
	w.free()


# T29: event bus — subscribe_event / unsubscribe_event and the emit count.
func _test_t29_event_bus(t: RefCounted) -> void:
	print("-- T29: event bus --")
	var w: VECSWorld = VECSWorld.new()
	var received := []
	var sid: int = w.subscribe_event("hello", func(ent: VECSEntity, payload: Variant) -> void:
		received.append(payload))
	t.expect(sid > 0, "T29: event subscription id")
	var e: VECSEntity = w.create_entity()
	var n: int = w.emit_event("hello", e, {"n": 1})
	t.expect_eq(n, 1, "T29: emit_event returns receiver count")
	t.expect_eq(received.size(), 1, "T29: event received")
	t.expect_eq(int(received[0]["n"]), 1, "T29: payload passed through")
	w.emit_event("world", e, {})
	t.expect_eq(received.size(), 1, "T29: other-name event not received")
	w.unsubscribe_event(sid)
	var n2: int = w.emit_event("hello", e, {})
	t.expect_eq(n2, 0, "T29: unsubscribed event has no receivers")
	t.expect_eq(received.size(), 1, "T29: no more events after unsubscribe")
	w.free()


# T30: get_debug_stats dictionary + query execution time.
func _test_t30_debug_stats(t: RefCounted) -> void:
	print("-- T30: debug stats --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T30C", [{"name": "v", "type": "I32"}])
	for i in 3:
		var e: VECSEntity = w.create_entity()
		e.add_component("T30C", {"v": i})
	var stats: Dictionary = w.get_debug_stats()
	t.expect_eq(int(stats["entity_count"]), 3, "T30: entity_count in stats")
	t.expect(int(stats["archetype_count"]) >= 2, "T30: archetype_count in stats")
	t.expect(int(stats["component_count"]) >= 1, "T30: component_count in stats")
	t.expect(int(stats["change_tick"]) > 0, "T30: change_tick in stats")
	var q: VECSQueryBuilder = w.query().with_all(["T30C"])
	q.execute()
	t.expect(int(q.get_last_execution_time_usec()) >= 0, "T30: query exec time reported")
	w.free()


# T31: ChangeView::take() (via the C++ ViewSystem) — pre-existing entities are
# reported exactly once, a second take with no writes is empty, and multiple
# writes to one entity between takes deduplicate to a single report.
func _test_t31_changeview_log(t: RefCounted) -> void:
	print("-- T31: ChangeView log consistency --")
	var w: VECSWorld = VECSWorld.new()
	# Position / Velocity are the C++ demo components registered at init.
	var vs: ViewSystem = ViewSystem.new()
	vs.group = "t31"
	w.add_system(vs)
	for i in 3:
		var e: VECSEntity = w.create_entity()
		e.add_component("Position", {"x": 0.0, "y": 0.0, "z": 0.0})
	w.process(0.016, "t31")
	t.expect_eq(int(vs.get_changed_count()), 3, "T31: first take reports all pre-existing")
	w.process(0.016, "t31")
	t.expect_eq(int(vs.get_changed_count()), 0, "T31: second take with no writes is empty")
	var ents: Array = w.query().with_all(["Position"]).execute()
	ents[0].get_component("Position").set_field("x", 1.0)
	w.process(0.016, "t31")
	t.expect_eq(int(vs.get_changed_count()), 1, "T31: one write -> one changed entity")
	var c: VECSComponent = ents[0].get_component("Position")
	c.set_field("x", 2.0)
	c.set_field("x", 3.0)
	w.process(0.016, "t31")
	t.expect_eq(int(vs.get_changed_count()), 1, "T31: multiple writes to one entity dedup to one")
	w.remove_system(vs)
	vs.free()
	w.free()


# T32: sync_priority delta throttling. A MEDIUM (0.1s) networked field sent over
# ~0.5s reaches the client only a handful of times, not once per write.
func _test_t32_sync_throttle(t: RefCounted) -> void:
	print("-- T32: sync_priority throttle --")
	var sw: VECSWorld = VECSWorld.new()
	sw.register_component("NetT32", [{"name": "v", "type": "F32", "sync_priority": 2}])  # MEDIUM
	var cw: VECSWorld = VECSWorld.new()
	var s_ns: VECSNetworkSync = VECSNetworkSync.new()
	var c_ns: VECSNetworkSync = VECSNetworkSync.new()
	s_ns.set_server(true)
	s_ns.bind_world(sw)
	c_ns.bind_world(cw)
	s_ns.set_direct_peer(c_ns)
	var se: VECSEntity = sw.create_entity()
	se.add_component("NetT32", {"v": 0.0})
	s_ns.tick(0.016)  # spawn
	var ce: VECSEntity = cw.query().with_all(["NetT32"]).execute()[0]
	var last_val: float = ce.get_component("NetT32").get_field("v")
	var updates := 0
	for i in 40:
		se.get_component("NetT32").set_field("v", float(i))
		s_ns.tick(0.013)  # ~0.52s total
		var cur: float = ce.get_component("NetT32").get_field("v")
		if cur != last_val:
			updates += 1
			last_val = cur
	t.expect(updates >= 3 and updates <= 8, "T32: MEDIUM throttled to ~5 updates (got %d)" % updates)
	s_ns.free()
	c_ns.free()
	cw.free()
	sw.free()


# T33: verbose logging flag — VECS.set_verbose / is_verbose and the
# vortarisecs/verbose project setting they write.
func _test_t33_verbose_flag(t: RefCounted) -> void:
	if not OS.is_debug_build():
		print("skip T33 (release build)")
		return
	print("-- T33: verbose logging flag --")
	var w: VECSWorld = VECS.get_world()
	t.expect_eq(w.is_verbose(), false, "T33: verbose off by default")
	w.set_verbose(true)
	t.expect_eq(w.is_verbose(), true, "T33: set_verbose(true) enables verbose")
	t.expect_eq(bool(ProjectSettings.get_setting("vortarisecs/verbose", false)), true, "T33: project setting written on")
	w.set_verbose(false)
	t.expect_eq(w.is_verbose(), false, "T33: set_verbose(false) disables verbose")
	t.expect_eq(bool(ProjectSettings.get_setting("vortarisecs/verbose", false)), false, "T33: project setting reset")


# T34: loading a partially-broken snapshot must NOT clear the live world. A save
# with 1 good + 1 bad (unregistered component) entity fails the load, and the
# previous world has to be fully restored — not left cleared and half-rebuilt.
func _test_t34_bad_snapshot_preserves_world(t: RefCounted) -> void:
	print("-- T34: failed JSON load preserves the live world --")
	var w: VECSWorld = VECSWorld.new()
	w.register_component("T34C", [{"name": "v", "type": "I32"}])
	var e1: VECSEntity = w.create_entity()
	e1.add_component("T34C", {"v": 10})
	var e2: VECSEntity = w.create_entity()
	e2.add_component("T34C", {"v": 20})
	t.expect_eq(w.entity_count(), 2, "T34: baseline world has 2 entities")

	var live_save: Dictionary = w.serialize_snapshot_json()
	# 1 good entity (registered component) + 1 bad entity (unregistered
	# component). spawn_from_data skips the bad one, so the load must fail.
	var bad_save: Dictionary = {
		"version": live_save["version"],
		"entities": [
			{"components": {"T34C": {"v": 99}}},
			{"components": {"DoesNotExist": {"x": 1.0}}},
		],
	}
	var ok: bool = w.deserialize_snapshot_json(JSON.stringify(bad_save))
	t.expect_eq(ok, false, "T34: corrupt save rejected (returns false)")

	# The live world must be fully restored: same count, ids, and values.
	t.expect_eq(w.entity_count(), 2, "T34: live world preserved (entity_count)")
	var ids := {}
	var q: Array = w.query().with_all(["T34C"]).execute()
	for ent in q:
		ids[ent.get_id()] = int(ent.get_component("T34C").get_field("v"))
	t.expect_eq(q.size(), 2, "T34: both original entities still alive")
	t.expect_eq(ids[e1.get_id()], 10, "T34: e1 value preserved")
	t.expect_eq(ids[e2.get_id()], 20, "T34: e2 value preserved")

	# The mapped variant follows the same contract: empty mapping on failure and
	# the previous world restored.
	var w2: VECSWorld = VECSWorld.new()
	w2.register_component("T34C", [{"name": "v", "type": "I32"}])
	var m: VECSEntity = w2.create_entity()
	m.add_component("T34C", {"v": 7})
	var mapping: Dictionary = w2.deserialize_snapshot_json_mapped(JSON.stringify(bad_save))
	t.expect_eq(mapping.size(), 0, "T34: mapped corrupt load returns empty mapping")
	t.expect_eq(w2.entity_count(), 1, "T34: mapped corrupt load preserved the live world")
	t.expect_eq(int(w2.entity(m.get_id()).get_component("T34C").get_field("v")), 7, "T34: mapped world value preserved")

	w.free()
	w2.free()
