extends Node

func _ready() -> void:
	print("=== VortarisECS Demo ===")
	var world: VECSWorld = VECS.get_world()
	print("world class: ", world.get_class())
	print("entity_count at start = ", world.entity_count())

	# --- create entities and add components ---
	var e1: VECSEntity = world.create_entity()
	e1.add_component("Position", {"x": 1.0, "y": 2.0, "z": 0.0})
	e1.add_component("Velocity", {"x": 0.5, "y": 0.0, "z": 0.0})

	var e2: VECSEntity = world.create_entity()
	e2.add_component("Position", {"x": 10.0, "y": 20.0, "z": 0.0})

	print("entity_count after 2 creates = ", world.entity_count())
	print("e1 alive = ", e1.is_alive(), ", id = ", e1.get_id())

	# --- read / write fields ---
	var pos: VECSComponent = e1.get_component("Position")
	print("e1.position = ", pos.get_fields())
	print("e1.position.x = ", pos.get_field("x"))
	pos.set_field("x", 3.0)
	print("e1.position.x after set_field = ", pos.get_field("x"))

	# --- component type metadata ---
	var ct: VECSComponentType = world.get_component_type("Position")
	print("Position fields = ", ct.get_field_names(), ", size = ", ct.get_size())

	# --- query ---
	var both: Array = world.query().with_all(["Position", "Velocity"]).execute()
	print("entities with Position+Velocity = ", both.size())
	var all_pos: Array = world.query().with_all(["Position"]).execute()
	print("entities with Position = ", all_pos.size())

	# --- command buffer (deferred structural changes) ---
	var cmd: VECSCommandBuffer = world.commands()
	cmd.add_component(e2, "Velocity", {"x": -1.0, "y": 0.0, "z": 0.0})
	print("cmd size before flush = ", cmd.size())
	cmd.flush()
	print("after flush, e2 has Velocity = ", e2.has_component("Velocity"))

	# --- destroy ---
	world.destroy_entity(e2)
	print("e2 alive after destroy = ", e2.is_alive())

	# --- system scheduling (M4) ---
	var ms: MoveSystem = MoveSystem.new()
	ms.group = "physics"
	ms.tick_interval = 0.0
	world.add_system(ms)
	print("system_count = ", world.system_count())

	# Create more entities the system will move. e1 (Position+Velocity) is alive.
	for i in 3:
		var e: VECSEntity = world.create_entity()
		e.add_component("Position", {"x": 10.0 * (i + 1), "y": 0.0, "z": 0.0})
		e.add_component("Velocity", {"x": 2.0, "y": 0.0, "z": 0.0})

	var before: Array = world.query().with_all(["Position", "Velocity"]).execute()
	print("entities with Position+Velocity before process = ", before.size())

	world.process(0.1, "physics")
	print("MoveSystem processed = ", ms.get_processed_count())

	var p0: VECSComponent = before[0].get_component("Position")
	print("first entity position.x after move = ", p0.get_field("x"))

	# --- change detection (.changed()) ---
	world.process(0.016, "physics")  # advance the write clock
	var ch1: Array = world.query().with_all(["Position"]).changed(["Position"]).execute()
	print("changed() first pass = ", ch1.size())
	# Modify only one entity's Position, then re-query: only that one should be "changed".
	var target: VECSEntity = before[0]
	var tp: VECSComponent = target.get_component("Position")
	tp.set_field("y", 42.0)
	var ch2: Array = world.query().with_all(["Position"]).changed(["Position"]).execute()
	print("changed() second pass (only the edited entity) = ", ch2.size())

	# --- observers / events (M5) ---
	var obs = preload("res://scripts/test_observer.gd").new()
	obs.on_added()
	obs.on_changed()
	obs.on_removed()
	obs.set_components(["Position"])
	obs.name = "PosObserver"
	world.add_observer(obs)

	var oe: VECSEntity = world.create_entity()
	oe.add_component("Position", {"x": 100.0, "y": 0.0, "z": 0.0})  # ADDED
	var oep: VECSComponent = oe.get_component("Position")
	oep.set_field("x", 200.0)                                       # CHANGED
	oe.remove_component("Position")                                 # REMOVED
	print("position observer events = ", obs.event_log)

	var cobs = preload("res://scripts/test_observer.gd").new()
	cobs.on_custom()
	cobs.set_custom_event_name("damage")
	world.add_observer(cobs)
	world.emit_event("damage", oe, {"amount": 5})
	world.emit_event("other", oe, {"amount": 9})  # wrong name -> ignored
	print("custom observer events = ", cobs.event_log)

	world.remove_observer(obs)
	obs.free()
	world.remove_observer(cobs)
	cobs.free()

	# --- deterministic snapshot serialization (M6) ---
	var bytes: PackedByteArray = world.serialize_snapshot()
	print("snapshot bytes = ", bytes.size())

	var world2: VECSWorld = VECSWorld.new()
	var ok: bool = world2.deserialize_snapshot(bytes)
	print("deserialize ok = ", ok, ", entity_count = ", world2.entity_count())
	var q2: Array = world2.query().with_all(["Position", "Velocity"]).execute()
	print("world2 Position+Velocity = ", q2.size())
	var p2: VECSComponent = q2[0].get_component("Position")
	print("world2 first position = ", p2.get_fields())
	world2.free()

	# determinism: serializing twice yields identical bytes
	var bytes_again: PackedByteArray = world.serialize_snapshot()
	print("serialization deterministic = ", bytes == bytes_again)

	# --- network replication (M7, direct transport test) ---
	var client_world: VECSWorld = VECSWorld.new()
	var server_ns: VECSNetworkSync = VECSNetworkSync.new()
	var client_ns: VECSNetworkSync = VECSNetworkSync.new()
	server_ns.set_server(true)
	server_ns.bind_world(world)
	client_ns.bind_world(client_world)
	server_ns.set_direct_peer(client_ns)

	var ne: VECSEntity = world.create_entity()
	ne.add_component("Position", {"x": 1.0, "y": 2.0, "z": 0.0})
	ne.add_component("Velocity", {"x": 5.0, "y": 0.0, "z": 0.0})
	server_ns.tick(0.1)  # sends spawn + any seeded entities
	var cq: Array = client_world.query().with_all(["Position", "Velocity"]).execute()
	print("client replicated entities after spawn = ", cq.size())

	# Find the client's copy of `ne` by id, then verify delta propagation.
	var ne_id: int = ne.get_id()
	var c_ent: VECSEntity = null
	for ent in cq:
		if ent.get_id() == ne_id:
			c_ent = ent
			break
	print("client found replicated entity = ", c_ent != null)
	var np2: VECSComponent = ne.get_component("Position")
	np2.set_field("x", 99.0)
	print("server position.x after set = ", np2.get_field("x"))
	server_ns.tick(0.1)  # sends delta
	var cp2: VECSComponent = c_ent.get_component("Position")
	print("client position.x after delta (cached handle) = ", cp2.get_field("x"))
	# Re-query the client world to rule out a stale handle.
	var cq2: Array = client_world.query().with_all(["Position", "Velocity"]).execute()
	for ent2 in cq2:
		if ent2.get_id() == ne_id:
			var p2b: VECSComponent = ent2.get_component("Position")
			print("client position.x after delta (re-query) = ", p2b.get_field("x"))
			break

	world.destroy_entity(ne)
	server_ns.tick(0.1)  # sends despawn
	var cq_after: Array = client_world.query().with_all(["Position", "Velocity"]).execute()
	print("client Position+Velocity after despawn = ", cq_after.size())

	server_ns.free()
	client_ns.free()
	client_world.free()

	# --- GDScript schema component + script system (fully scriptable) ---
	var ok_health: bool = world.register_component("Health", [
		{"name": "amount", "type": "F32"},
		{"name": "max", "type": "F32", "sync_priority": 0},
	])
	print("register Health (schema) = ", ok_health)

	var he: VECSEntity = world.create_entity()
	he.add_component("Health", {"amount": 50.0, "max": 100.0})
	var hc: VECSComponent = he.get_component("Health")
	print("health.amount = ", hc.get_field("amount"))
	hc.set_field("amount", 75.0)
	print("health.amount after set = ", hc.get_field("amount"))

	var gsys = preload("res://scripts/script_system.gd").new()
	gsys.group = "scripts"
	world.add_system(gsys)
	var se: VECSEntity = world.create_entity()
	se.add_component("Position", {"x": 0.0, "y": 0.0, "z": 0.0})
	se.add_component("Velocity", {"x": 10.0, "y": 0.0, "z": 0.0})
	world.process(0.1, "scripts")
	var sep: VECSComponent = se.get_component("Position")
	print("script-moved position.x = ", sep.get_field("x"))  # expect 1.0

	# --- 活跃集 + 事件驱动:沙子下落 (事件进入活跃集,系统只处理活跃集) ---
	world.register_component("Falling", [{"name": "time", "type": "F32"}])

	var sand_obs = preload("res://scripts/sand_observer.gd").new()
	sand_obs.on_custom()
	sand_obs.set_custom_event_name("sand_support_broken")
	world.add_observer(sand_obs)

	var fall_sys = preload("res://scripts/falling_system.gd").new()
	fall_sys.group = "sand"
	world.add_system(fall_sys)

	var sand: VECSEntity = world.create_entity()
	sand.add_component("Position", {"x": 0.0, "y": 10.0, "z": 0.0})
	print("sand y before = ", sand.get_component("Position").get_field("y"))

	world.emit_event("sand_support_broken", sand, {})   # 事件 → 加入活跃集(Falling)
	world.process(0.5, "sand")                          # 只处理活跃集,下落 0.5s
	print("sand y after 0.5s = ", sand.get_component("Position").get_field("y"))  # 期望 7.5

	# --- C++ View(预缓存) + ChangeView(变更感知) ---
	var vs: ViewSystem = ViewSystem.new()
	vs.group = "viewtest"
	world.add_system(vs)
	world.process(0.016, "viewtest")
	print("ViewSystem view_count = ", vs.get_view_count())
	var tv: VECSEntity = world.query().with_all(["Position"]).execute_one()
	var tvp: VECSComponent = tv.get_component("Position")
	tvp.set_field("x", 123.0)                            # 制造一次 Position 变更
	world.process(0.016, "viewtest")
	print("ViewSystem changed_count = ", vs.get_changed_count())  # 期望 ≥1

	# --- JSON 序列化 / 数据表 (深度绑定 Godot 自带 JSON) ---
	# 卡牌:批量注册组件 schema + 从数据表批量生成实体
	world.register_components({
		"Card":   [{"name": "title", "type": "StringFixed", "count": 64}],
		"Effect": [{"name": "damage", "type": "F32"}, {"name": "kind", "type": "I32"}],
		"Cost":   [{"name": "mana", "type": "I32"}],
	})
	var deck: Array = world.spawn_from_data([
		{"components": {"Card": {"title": "火球术"}, "Effect": {"damage": 15.0}, "Cost": {"mana": 3}}},
		{"components": {"Card": {"title": "治疗术"}, "Effect": {"kind": 2}, "Cost": {"mana": 2}}},
	])
	print("spawned cards = ", deck.size())
	var card_title: VECSComponent = deck[0].get_component("Card")
	print("card0 title = ", card_title.get_field("title"), ", damage = ", deck[0].get_component("Effect").get_field("damage"))

	# JSON 世界存档:序列化 → JSON.stringify → parse → 反序列化到新世界
	var save_dict: Dictionary = world.serialize_snapshot_json()
	var save_text: String = JSON.stringify(save_dict, "\t")
	print("save json length = ", save_text.length())
	var loaded_world: VECSWorld = VECSWorld.new()
	var load_ok: bool = loaded_world.deserialize_snapshot_json(save_text)   # 直接吃 JSON 字符串
	print("json load ok = ", load_ok, ", loaded entities = ", loaded_world.entity_count())
	var exported: Array = loaded_world.entities_to_data()
	print("exported entities = ", exported.size())
	loaded_world.free()

	world.remove_system(vs); vs.free()
	world.remove_observer(sand_obs); sand_obs.free()
	world.remove_system(fall_sys); fall_sys.free()

	world.remove_system(ms)
	ms.free()
	world.remove_system(gsys)
	gsys.free()
	print("entity_count at end = ", world.entity_count())

	print("=== VortarisECS Demo OK ===")
	get_tree().quit(0)
