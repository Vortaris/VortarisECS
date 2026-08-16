#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

#include "core/component_schema.h"
#include "demo/systems.h"
#include "gdscript/vecs_command_buffer.h"
#include "gdscript/vecs_component.h"
#include "gdscript/vecs_component_type.h"
#include "gdscript/vecs_entity.h"
#include "gdscript/vecs_log.h"
#include "gdscript/vecs_observer.h"
#include "gdscript/vecs_query_builder.h"
#include "gdscript/vecs_settings.h"
#include "gdscript/vecs_system.h"
#include "gdscript/vecs_world.h"
#include "network/network_sync.h"

// Demo component/systems compiled into the same dll. Remove/replace when the
// framework is consumed by another extension.
void vortaris_demo_register_components();

using namespace godot;

static VECSWorld *g_vecs_singleton = nullptr;

namespace {

// Registers one hierarchical project setting (`vortarisecs/<category>/<name>`)
// so it shows up in the Project Settings editor with the right type/hint and a
// seeded default. `set_setting` must come first: `add_property_info` only
// attaches editor metadata to an existing setting. The default is written only
// when the setting is absent, so a user's baked-in value is never clobbered
// (mirrors the ModLoader F4 fix). `set_initial_value` seeds the editor's
// "reset to default" target so a reset restores the documented default instead
// of null (0.3.0 E5 fix).
void register_setting(const char *p_name, const Variant &p_default, Variant::Type p_type,
		PropertyHint p_hint, const String &p_hint_string) {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (!ps) {
		return;
	}
	if (!ps->has_setting(StringName(p_name))) {
		ps->set_setting(StringName(p_name), p_default);
	}
	ps->set_initial_value(StringName(p_name), p_default);
	Dictionary pi;
	pi["name"] = StringName(p_name);
	pi["type"] = p_type;
	pi["hint"] = p_hint;
	pi["hint_string"] = p_hint_string;
	ps->add_property_info(pi);
}

} // namespace

void initialize_vortarisecs_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(VECSWorld);
	GDREGISTER_CLASS(VECSEntity);
	GDREGISTER_CLASS(VECSComponent);
	GDREGISTER_CLASS(VECSComponentType);
	GDREGISTER_CLASS(VECSQueryBuilder);
	GDREGISTER_CLASS(VECSCommandBuffer);
	GDREGISTER_VIRTUAL_CLASS(VECSSystem);
	// VECSObserver is intentionally a CONCRETE class since 0.3.1: it must be
	// directly instantiable from GDScript (`VECSObserver.new()`) so CHANT can
	// build observers without subclassing. Subclassing (GDScript `_script_each`
	// override, or C++ `_each`) still works, and a plain set_callback() callable
	// is the no-subclass path.
	GDREGISTER_CLASS(VECSObserver);
	GDREGISTER_CLASS(MoveSystem);
	GDREGISTER_CLASS(ViewSystem);
	GDREGISTER_CLASS(VECSSyncStrategy);
	GDREGISTER_CLASS(VECSSnapshotReplication);
	GDREGISTER_CLASS(VECSNetworkSync);

	vortaris_demo_register_components();

	// Register the hierarchical project settings so they show up in the editor
	// (Project Settings > VortarisECS) and survive into exported builds. Only
	// defaults are seeded (never clobbering a user's baked-in value).
	//
	// `vortarisecs/general/verbose` is special-cased: the legacy flat
	// `vortarisecs/verbose` (0.3.0) migrates into it so an upgraded project
	// keeps its choice. get_verbose_setting() still falls back to the legacy
	// path as a safety net.
	ProjectSettings *ps = ProjectSettings::get_singleton();
	if (ps) {
		if (!ps->has_setting("vortarisecs/general/verbose")) {
			bool legacy_verbose = false;
			if (ps->has_setting("vortarisecs/verbose")) {
				legacy_verbose = (bool)ps->get_setting("vortarisecs/verbose", false);
			}
			ps->set_setting("vortarisecs/general/verbose", legacy_verbose);
		}
		Dictionary verbose_hint;
		verbose_hint["name"] = "vortarisecs/general/verbose";
		verbose_hint["type"] = Variant::BOOL;
		verbose_hint["hint"] = PropertyHint::PROPERTY_HINT_NONE;
		verbose_hint["hint_string"] = String("Detailed verbose logging (debug builds only; migrated from vortarisecs/verbose in 0.3.0).");
		ps->add_property_info(verbose_hint);
		// E5: seed the editor's "reset" target so resetting verbose restores the
		// effective (possibly migrated) default rather than null.
		ps->set_initial_value("vortarisecs/general/verbose", (bool)ps->get_setting("vortarisecs/general/verbose", false));

		register_setting("vortarisecs/general/auto_shutdown_on_exit", Variant(true), Variant::BOOL,
				PropertyHint::PROPERTY_HINT_NONE,
				String("Auto-clean static resources (VECSWorld::shutdown) when the extension unloads."));
		register_setting("vortarisecs/general/max_snapshot_entities", Variant((int64_t)500), Variant::INT,
				PropertyHint::PROPERTY_HINT_RANGE, String("1,100000,1"));
		register_setting("vortarisecs/debug/auto_refresh_interval", Variant(1.0), Variant::FLOAT,
				PropertyHint::PROPERTY_HINT_RANGE, String("0.1,60.0,0.1"));
		register_setting("vortarisecs/network/default_sync_priority",
				Variant((int64_t)vortaris::SYNC_MEDIUM), Variant::INT,
				PropertyHint::PROPERTY_HINT_ENUM,
				String("Realtime,High,Medium,Low,SpawnOnly,Local"));
		register_setting("vortarisecs/observer/default_throttle_tick", Variant((int64_t)0), Variant::INT,
				PropertyHint::PROPERTY_HINT_RANGE, String("0,1000000,1"));
		register_setting("vortarisecs/serialization/compact_json", Variant(false), Variant::BOOL,
				PropertyHint::PROPERTY_HINT_NONE,
				String("Write compact (unindented) JSON for snapshot JSON strings."));
	}
	vortaris::refresh_verbose();

	g_vecs_singleton = memnew(VECSWorld);
	Engine::get_singleton()->register_singleton("VECS", g_vecs_singleton);

	// Register the EngineDebugger message capture so the editor's remote ECS
	// monitor (addons/vortarisecs/editor/ecs_debugger_*.gd) can request world
	// snapshots from the RUNNING game. The editor process itself is skipped (its
	// world is empty and it must not listen to its own debugger); in a standalone
	// or headless run EngineDebugger is inactive, so the capture is a no-op until
	// a debugger actually connects.
	if (!Engine::get_singleton()->is_editor_hint()) {
		EngineDebugger *dbg = EngineDebugger::get_singleton();
		if (dbg) {
			dbg->register_message_capture("vecs", callable_mp(g_vecs_singleton, &VECSWorld::_debugger_capture));
		}
	}
}

void uninitialize_vortarisecs_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (g_vecs_singleton) {
		// Unregister the debugger capture BEFORE deleting the world: the Callable
		// holds a raw pointer to the singleton, so calling it after memdelete
		// would be use-after-free. Mirrors the registration guard: the editor
		// process never registered it, so it must not be unregistered here.
		if (!Engine::get_singleton()->is_editor_hint()) {
			EngineDebugger *dbg = EngineDebugger::get_singleton();
			if (dbg) {
				dbg->unregister_message_capture("vecs");
			}
		}
		// Tear down the world (deferred ops, observer callbacks, scheduler,
		// change baselines) BEFORE clearing the component registry: the world's
		// cleanup must not touch already-cleared StringName schema names.
		// Gated by `vortarisecs/general/auto_shutdown_on_exit` (default true)
		// so projects can opt out of the explicit cleanup.
		if (vortaris::get_auto_shutdown_on_exit()) {
			g_vecs_singleton->shutdown();
		}
		Engine::get_singleton()->unregister_singleton("VECS");
		memdelete(g_vecs_singleton);
		g_vecs_singleton = nullptr;
	}
	vortaris::ComponentRegistry::instance().clear();
}

extern "C" {
// Initialization.
GDExtensionBool GDE_EXPORT vortarisecs_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

	init_obj.register_initializer(initialize_vortarisecs_module);
	init_obj.register_terminator(uninitialize_vortarisecs_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
} // extern "C"
