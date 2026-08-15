#include "vecs_world.h"

#include <algorithm>
#include <unordered_set>

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "../core/archetype.h"
#include "../core/entity.h"
#include "../reflect/type_traits.h"
#include "../serialization/binary_buffer.h"
#include "../serialization/snapshot.h"
#include "vecs_command_buffer.h"
#include "vecs_component_type.h"
#include "vecs_entity.h"
#include "vecs_observer.h"
#include "vecs_query_builder.h"
#include "vecs_system.h"

VECSWorld::VECSWorld() :
		core_(new vortaris::World()),
		scheduler_(new vortaris::SystemScheduler()) {
}

godot::Ref<VECSEntity> VECSWorld::create_entity() {
	vortaris::Entity e = core_->create_entity();
	return VECSEntity::make(core_.get(), e);
}

godot::Ref<VECSEntity> VECSWorld::create_entity_preassigned(int64_t p_id) {
	if (p_id <= 0) {
		ERR_PRINT("VortarisECS: create_entity_preassigned requires a positive id.");
		return godot::Ref<VECSEntity>();
	}
	vortaris::Entity e = core_->create_entity_preassigned(static_cast<uint64_t>(p_id));
	if (!e) {
		ERR_PRINT("VortarisECS: create_entity_preassigned rejected id (slot out of range or already occupied).");
		return godot::Ref<VECSEntity>();
	}
	return VECSEntity::make(core_.get(), e);
}

godot::Ref<VECSEntity> VECSWorld::entity(int64_t p_id) const {
	if (p_id <= 0) {
		return godot::Ref<VECSEntity>();
	}
	vortaris::Entity e{ static_cast<uint64_t>(p_id) };
	if (!core_->is_alive(e)) {
		return godot::Ref<VECSEntity>();
	}
	return VECSEntity::make(core_.get(), e);
}

bool VECSWorld::has_entity(int64_t p_id) const {
	if (p_id <= 0) {
		return false;
	}
	return core_->is_alive(vortaris::Entity{ static_cast<uint64_t>(p_id) });
}

void VECSWorld::destroy_entity(const godot::Ref<VECSEntity> &p_entity) {
	if (p_entity.is_valid()) {
		core_->destroy_entity(p_entity->entity());
	}
}

bool VECSWorld::is_alive(const godot::Ref<VECSEntity> &p_entity) const {
	if (!p_entity.is_valid()) {
		return false;
	}
	return core_->is_alive(p_entity->entity());
}

int64_t VECSWorld::entity_count() const {
	return static_cast<int64_t>(core_->entity_count());
}

void VECSWorld::set_entity_range(int64_t p_base) {
	// The core reserves slot_generations_ up to this many slots; anything huge
	// would allocate a large generation array for no reason.
	constexpr int64_t k_max_base = (int64_t(1) << 24);
	if (p_base < 0 || p_base > k_max_base) {
		ERR_PRINT("VortarisECS: set_entity_range out of range (0 .. " + godot::String::num_int64(k_max_base) + ").");
		return;
	}
	core_->set_entity_range(static_cast<uint32_t>(p_base));
}

bool VECSWorld::register_component(const godot::String &p_name, const godot::Array &p_fields) {
	std::vector<vortaris::FieldDescriptor> fds;
	fds.reserve(static_cast<size_t>(p_fields.size()));
	std::unordered_set<godot::StringName> seen_names;
	for (int i = 0; i < p_fields.size(); ++i) {
		if (p_fields[i].get_type() != godot::Variant::DICTIONARY) {
			ERR_PRINT("VortarisECS: each field of register_component must be a Dictionary {name, type, ...}.");
			return false;
		}
		godot::Dictionary d = p_fields[i];
		godot::String fname = d.get("name", "");
		godot::String ftype = d.get("type", "");
		if (fname.is_empty() || ftype.is_empty()) {
			ERR_PRINT("VortarisECS: field needs both 'name' and 'type'.");
			return false;
		}
		if (!seen_names.insert(godot::StringName(fname)).second) {
			ERR_PRINT("VortarisECS: duplicate field name '" + fname + "' in component '" + p_name + "'.");
			return false;
		}
		vortaris::FieldType t;
		if (!vortaris::ComponentRegistry::parse_field_type(ftype, t)) {
			ERR_PRINT("VortarisECS: unknown field type '" + ftype + "'.");
			return false;
		}
		vortaris::FieldDescriptor fd;
		fd.name = godot::StringName(fname);
		fd.type = t;
		int64_t count = d.get("count", (int64_t)1);
		fd.count = count > 0 ? static_cast<size_t>(count) : 1;
		fd.sync_priority = static_cast<uint8_t>((int64_t)d.get("sync_priority", (int64_t)vortaris::SYNC_MEDIUM));
		fd.is_networked = (bool)d.get("networked", true);
		fds.push_back(fd);
	}
	return core_->registry().register_schema_component(godot::StringName(p_name), fds) != vortaris::INVALID_COMPONENT_TYPE;
}

godot::Ref<VECSComponentType> VECSWorld::get_component_type(const godot::String &p_name) {
	vortaris::ComponentTypeId t = core_->registry().id_of(godot::StringName(p_name));
	if (t == vortaris::INVALID_COMPONENT_TYPE) {
		return godot::Ref<VECSComponentType>();
	}
	return VECSComponentType::make(t);
}

godot::Ref<VECSQueryBuilder> VECSWorld::query() {
	return VECSQueryBuilder::make(core_.get());
}

godot::Ref<VECSCommandBuffer> VECSWorld::commands() {
	return VECSCommandBuffer::make(core_.get());
}

godot::Ref<VECSEntity> VECSWorld::spawn(const godot::Dictionary &p_components) {
	godot::Dictionary entry;
	entry["components"] = p_components;
	godot::Array list;
	list.append(entry);
	godot::Array spawned = spawn_from_data(list);
	if (spawned.size() == 0) {
		return godot::Ref<VECSEntity>();
	}
	return spawned[0];
}

void VECSWorld::each(const godot::Array &p_components, const godot::Callable &p_callable) {
	std::vector<vortaris::ComponentTypeId> ids;
	for (int i = 0; i < p_components.size(); ++i) {
		vortaris::ComponentTypeId t = core_->registry().id_of(godot::StringName(godot::String(p_components[i])));
		if (t != vortaris::INVALID_COMPONENT_TYPE) {
			ids.push_back(t);
		}
	}
	if (ids.empty()) {
		ERR_PRINT("VortarisECS: each() needs at least one known component name.");
		return;
	}
	vortaris::Query q;
	q.all = std::move(ids);
	std::sort(q.all.begin(), q.all.end());
	const auto &arches = core_->query_cache().match(q, core_->all_archetypes());
	core_->begin_iteration();
	for (const vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			p_callable.call(VECSEntity::make(core_.get(), a->entities[row]));
		}
	}
	core_->end_iteration();
}

godot::Variant VECSWorld::get_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field) {
	if (p_entity.is_valid()) {
		return p_entity->getf(p_comp, p_field);
	}
	return godot::Variant();
}

void VECSWorld::set_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_value) {
	if (p_entity.is_valid()) {
		p_entity->setf(p_comp, p_field, p_value);
	}
}

void VECSWorld::add_system(VECSSystem *p_system) {
	if (!p_system) {
		return;
	}
	p_system->set_core_world(core_.get());
	p_system->set_world_node(this);
	p_system->_setup(*core_);
	scheduler_->add_system(p_system);
}

void VECSWorld::remove_system(VECSSystem *p_system) {
	if (!p_system) {
		return;
	}
	scheduler_->remove_system(p_system);
	p_system->set_core_world(nullptr);
}

int64_t VECSWorld::system_count() const {
	return static_cast<int64_t>(scheduler_->system_count());
}

void VECSWorld::add_observer(VECSObserver *p_observer) {
	if (!p_observer) {
		return;
	}
	vortaris::ObserverCallback cb;
	cb.event_mask = static_cast<uint32_t>(p_observer->get_events());
	cb.component_filter = p_observer->component_filter();
	cb.watch_all = p_observer->component_filter().empty();
	cb.custom_name = p_observer->get_custom_event_name();
	cb.fn = [this, p_observer](vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
		p_observer->handle_event(p_type, p_e, p_comp, p_name, p_payload);
		if ((p_type == vortaris::ObserverEventType::Added || p_type == vortaris::ObserverEventType::Removed) && p_observer->has_match_events()) {
			p_observer->evaluate_match(p_e);
		}
	};
	p_observer->set_world(this);
	p_observer->set_observer_id(core_->observer_dispatch().add(std::move(cb)));
	if (p_observer->has_match_events()) {
		p_observer->seed_membership();
	}
}

void VECSWorld::remove_observer(VECSObserver *p_observer) {
	if (!p_observer) {
		return;
	}
	if (p_observer->observer_id() > 0) {
		core_->observer_dispatch().remove(p_observer->observer_id());
		p_observer->set_observer_id(0);
	}
	p_observer->set_world(nullptr);
}

void VECSWorld::emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload) {
	vortaris::Entity e;
	if (p_entity.is_valid()) {
		e = p_entity->entity();
	}
	core_->emit_event(p_name, e, p_payload);
}

void VECSWorld::process(double p_delta, const godot::String &p_group) {
	// Advance the global write clock so that per-frame component writes get a
	// fresh change tick (required for .changed() queries), then run the group.
	core_->advance_change_tick();
	scheduler_->process(*core_, p_delta, p_group);
}

void VECSWorld::compact() {
	core_->compact();
}

void VECSWorld::shutdown() {
	core_->reset();
	scheduler_->clear();
}

VECSWorld *VECSWorld::get_world() {
	return this;
}

godot::PackedByteArray VECSWorld::serialize_snapshot() const {
	vortaris::BinaryBuffer buf;
	core_->serialize_snapshot(buf);
	return buf.to_packed();
}

bool VECSWorld::deserialize_snapshot(const godot::PackedByteArray &p_data) {
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_data);
	return core_->deserialize_snapshot(buf);
}

bool VECSWorld::register_components(const godot::Dictionary &p_components) {
	const godot::Array names = p_components.keys();
	for (int i = 0; i < names.size(); ++i) {
		const godot::String name = names[i];
		const godot::Array fields = p_components[name];
		if (!register_component(name, fields)) {
			return false;
		}
	}
	return true;
}

godot::Array VECSWorld::spawn_from_data(const godot::Array &p_entities) {
	godot::Array out;
	for (int i = 0; i < p_entities.size(); ++i) {
		if (p_entities[i].get_type() != godot::Variant::DICTIONARY) {
			ERR_PRINT("VortarisECS: spawn_from_data entry must be a Dictionary.");
			continue;
		}
		const godot::Dictionary edata = p_entities[i];
		godot::Ref<VECSEntity> ent;
		if (edata.has("id")) {
			const int64_t id = edata["id"];
			ent = create_entity_preassigned(id);
		} else {
			ent = create_entity();
		}
		if (!ent.is_valid()) {
			ERR_PRINT("VortarisECS: failed to create entity from data (id conflict?).");
			continue;
		}
		if (edata.has("components")) {
			const godot::Dictionary comps = edata["components"];
			const godot::Array comp_names = comps.keys();
			// Pre-flight: every named component must be registered, otherwise the
			// entity would be left partially initialized ("ghost entity").
			bool valid = true;
			for (int j = 0; j < comp_names.size(); ++j) {
				const godot::String cname = comp_names[j];
				if (core_->registry().id_of(godot::StringName(cname)) == vortaris::INVALID_COMPONENT_TYPE) {
					ERR_PRINT("VortarisECS: spawn_from_data component '" + cname + "' is not registered; entity skipped.");
					valid = false;
					break;
				}
			}
			if (!valid) {
				core_->destroy_entity(ent->entity());
				continue;
			}
			for (int j = 0; j < comp_names.size(); ++j) {
				const godot::String cname = comp_names[j];
				const godot::Dictionary fields = comps[cname];
				ent->add_component(cname, fields);
			}
		}
		out.append(ent);
	}
	return out;
}

godot::Array VECSWorld::entities_to_data() {
	godot::Array out;
	vortaris::World &w = core();
	std::vector<vortaris::Archetype *> arches = w.all_archetypes();
	std::sort(arches.begin(), arches.end(), [](const vortaris::Archetype *a, const vortaris::Archetype *b) {
		return a->signature < b->signature;
	});
	for (const vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			godot::Dictionary edata;
			edata["id"] = static_cast<int64_t>(a->entities[row].id);
			godot::Dictionary comps;
			for (size_t i = 0; i < a->component_ids.size(); ++i) {
				const vortaris::ComponentTypeId t = a->component_ids[i];
				const vortaris::ComponentSchema *s = vortaris::ComponentRegistry::instance().schema_of(t);
				if (!s) {
					continue;
				}
				godot::Dictionary fields;
				vortaris::component_bytes_to_variant_dict(*s, a->columns[i].row(row), fields);
				comps[godot::String(vortaris::ComponentRegistry::instance().name_of(t))] = fields;
			}
			edata["components"] = comps;
			out.append(edata);
		}
	}
	return out;
}

godot::Dictionary VECSWorld::serialize_snapshot_json() {
	godot::Dictionary out;
	out["version"] = static_cast<int64_t>(vortaris::SNAPSHOT_VERSION);
	out["entities"] = entities_to_data();
	return out;
}

bool VECSWorld::deserialize_snapshot_json(const godot::Variant &p_data) {
	godot::Dictionary root;
	if (p_data.get_type() == godot::Variant::STRING) {
		const godot::Variant parsed = godot::JSON::parse_string(p_data);
		if (parsed.get_type() != godot::Variant::DICTIONARY) {
			ERR_PRINT("VortarisECS: invalid JSON save data.");
			return false;
		}
		root = parsed;
	} else if (p_data.get_type() == godot::Variant::DICTIONARY) {
		root = p_data;
	} else {
		ERR_PRINT("VortarisECS: deserialize_snapshot_json expects a Dictionary or a JSON String.");
		return false;
	}

	if (root.has("version")) {
		const int64_t v = root["version"];
		if (v != static_cast<int64_t>(vortaris::SNAPSHOT_VERSION)) {
			ERR_PRINT("VortarisECS: save version mismatch (got " + godot::String::num_int64(v) + ", expected " + godot::String::num_int64(vortaris::SNAPSHOT_VERSION) + ").");
			return false;
		}
	}
	if (!root.has("entities")) {
		ERR_PRINT("VortarisECS: save data has no 'entities'.");
		return false;
	}
	const godot::Array ents = root["entities"];
	// Loading a save replaces the world: drop any existing entities first.
	core_->clear();
	const godot::Array spawned = spawn_from_data(ents);
	if (spawned.size() != ents.size()) {
		ERR_PRINT("VortarisECS: some entities failed to deserialize.");
		return false;
	}
	return true;
}

void VECSWorld::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("create_entity"), &VECSWorld::create_entity);
	ClassDB::bind_method(D_METHOD("create_entity_preassigned", "id"), &VECSWorld::create_entity_preassigned);
	ClassDB::bind_method(D_METHOD("entity", "id"), &VECSWorld::entity);
	ClassDB::bind_method(D_METHOD("has_entity", "id"), &VECSWorld::has_entity);
	ClassDB::bind_method(D_METHOD("destroy_entity", "entity"), &VECSWorld::destroy_entity);
	ClassDB::bind_method(D_METHOD("is_alive", "entity"), &VECSWorld::is_alive);
	ClassDB::bind_method(D_METHOD("entity_count"), &VECSWorld::entity_count);
	ClassDB::bind_method(D_METHOD("set_entity_range", "base"), &VECSWorld::set_entity_range);
	ClassDB::bind_method(D_METHOD("register_component", "name", "fields"), &VECSWorld::register_component);
	ClassDB::bind_method(D_METHOD("get_component_type", "name"), &VECSWorld::get_component_type);
	ClassDB::bind_method(D_METHOD("query"), &VECSWorld::query);
	ClassDB::bind_method(D_METHOD("commands"), &VECSWorld::commands);
	ClassDB::bind_method(D_METHOD("spawn", "components"), &VECSWorld::spawn);
	ClassDB::bind_method(D_METHOD("each", "components", "callable"), &VECSWorld::each);
	ClassDB::bind_method(D_METHOD("get_field", "entity", "comp", "field"), &VECSWorld::get_field);
	ClassDB::bind_method(D_METHOD("set_field", "entity", "comp", "field", "value"), &VECSWorld::set_field);
	ClassDB::bind_method(D_METHOD("add_system", "system"), &VECSWorld::add_system);
	ClassDB::bind_method(D_METHOD("remove_system", "system"), &VECSWorld::remove_system);
	ClassDB::bind_method(D_METHOD("system_count"), &VECSWorld::system_count);
	ClassDB::bind_method(D_METHOD("add_observer", "observer"), &VECSWorld::add_observer);
	ClassDB::bind_method(D_METHOD("remove_observer", "observer"), &VECSWorld::remove_observer);
	ClassDB::bind_method(D_METHOD("emit_event", "name", "entity", "payload"), &VECSWorld::emit_event, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("process", "delta", "group"), &VECSWorld::process, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("compact"), &VECSWorld::compact);
	ClassDB::bind_method(D_METHOD("shutdown"), &VECSWorld::shutdown);
	ClassDB::bind_method(D_METHOD("get_world"), &VECSWorld::get_world);
	ClassDB::bind_method(D_METHOD("serialize_snapshot"), &VECSWorld::serialize_snapshot);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot", "data"), &VECSWorld::deserialize_snapshot);
	ClassDB::bind_method(D_METHOD("register_components", "components"), &VECSWorld::register_components);
	ClassDB::bind_method(D_METHOD("spawn_from_data", "entities"), &VECSWorld::spawn_from_data);
	ClassDB::bind_method(D_METHOD("entities_to_data"), &VECSWorld::entities_to_data);
	ClassDB::bind_method(D_METHOD("serialize_snapshot_json"), &VECSWorld::serialize_snapshot_json);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot_json", "data"), &VECSWorld::deserialize_snapshot_json);
}
