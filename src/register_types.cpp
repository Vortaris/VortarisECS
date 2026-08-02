#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "demo/systems.h"
#include "gdscript/vecs_command_buffer.h"
#include "gdscript/vecs_component.h"
#include "gdscript/vecs_component_type.h"
#include "gdscript/vecs_entity.h"
#include "gdscript/vecs_observer.h"
#include "gdscript/vecs_query_builder.h"
#include "gdscript/vecs_system.h"
#include "gdscript/vecs_world.h"
#include "network/network_sync.h"

// Demo component/systems compiled into the same dll. Remove/replace when the
// framework is consumed by another extension.
void vortaris_demo_register_components();

using namespace godot;

static VECSWorld *g_vecs_singleton = nullptr;

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
	GDREGISTER_VIRTUAL_CLASS(VECSObserver);
	GDREGISTER_CLASS(MoveSystem);
	GDREGISTER_CLASS(VECSSyncStrategy);
	GDREGISTER_CLASS(VECSSnapshotReplication);
	GDREGISTER_CLASS(VECSNetworkSync);

	vortaris_demo_register_components();

	g_vecs_singleton = memnew(VECSWorld);
	Engine::get_singleton()->register_singleton("VECS", g_vecs_singleton);
}

void uninitialize_vortarisecs_module(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	if (g_vecs_singleton) {
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
