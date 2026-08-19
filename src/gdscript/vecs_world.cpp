#include "vecs_world.h"

#include <algorithm>
#include <unordered_set>

#include <godot_cpp/classes/engine_debugger.hpp>
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
#include "vecs_log.h"
#include "vecs_settings.h"
#include "vecs_component_type.h"
#include "vecs_entity.h"
#include "vecs_observer.h"
#include "vecs_query_builder.h"
#include "vecs_system.h"

VECSWorld::VECSWorld() :
		core_(new vortaris::World()),
		scheduler_(new vortaris::SystemScheduler()) {
	vortaris::log_debug("world created");
}

godot::Ref<VECSEntity> VECSWorld::create_entity() {
	vortaris::Entity e = core_->create_entity();
	if (vortaris::verbose_active()) {
		vortaris::log_verbose("spawn entity id=" + godot::String::num_int64(static_cast<int64_t>(e.id)));
	}
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
	if (vortaris::verbose_active()) {
		vortaris::log_verbose("spawn preassigned entity id=" + godot::String::num_int64(static_cast<int64_t>(e.id)));
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

godot::Ref<VECSEntity> VECSWorld::create_entity_pooled() {
	vortaris::Entity e = core_->create_entity_pooled();
	return VECSEntity::make(core_.get(), e);
}

void VECSWorld::destroy_entity_pooled(const godot::Ref<VECSEntity> &p_entity) {
	if (p_entity.is_valid()) {
		core_->destroy_entity_pooled(p_entity->entity());
	}
}

int64_t VECSWorld::pool_size() const {
	return static_cast<int64_t>(core_->pool_size());
}

void VECSWorld::destroy_entity(const godot::Ref<VECSEntity> &p_entity) {
	if (p_entity.is_valid()) {
		if (vortaris::verbose_active()) {
			vortaris::log_verbose("destroy entity id=" + godot::String::num_int64(static_cast<int64_t>(p_entity->entity().id)));
		}
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
		// Unspecified sync_priority falls back to the
		// `vortarisecs/network/default_sync_priority` project setting (clamped
		// to the valid SyncPriority range by the accessor).
		if (d.has("sync_priority")) {
			fd.sync_priority = static_cast<uint8_t>((int64_t)d["sync_priority"]);
		} else {
			fd.sync_priority = vortaris::get_default_sync_priority();
		}
		fd.is_networked = (bool)d.get("networked", true);
		fds.push_back(fd);
	}
	const vortaris::ComponentTypeId tid = core_->registry().register_schema_component(godot::StringName(p_name), fds);
	if (tid != vortaris::INVALID_COMPONENT_TYPE) {
		vortaris::log_verbose("registered component '" + p_name + "' (" + godot::String::num_int64(fds.size()) + " fields, type_id=" + godot::String::num_int64(tid) + ")");
	}
	return tid != vortaris::INVALID_COMPONENT_TYPE;
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

godot::Ref<VECSEntity> VECSWorld::create_with_components(int64_t p_def_id, const godot::Dictionary &p_components) {
	godot::Ref<VECSEntity> ent;
	if (p_def_id > 0) {
		ent = create_entity_preassigned(p_def_id);
	} else {
		ent = create_entity();
	}
	if (!ent.is_valid()) {
		return ent;
	}
	const godot::Array comp_names = p_components.keys();
	// Pre-flight: every named component must be registered, otherwise the entity
	// would be left partially initialized ("ghost entity").
	for (int j = 0; j < comp_names.size(); ++j) {
		const godot::String cname = comp_names[j];
		if (core_->registry().id_of(godot::StringName(cname)) == vortaris::INVALID_COMPONENT_TYPE) {
			ERR_PRINT("VortarisECS: create_with_components component '" + cname + "' is not registered; entity not spawned.");
			core_->destroy_entity(ent->entity());
			return godot::Ref<VECSEntity>();
		}
	}
	for (int j = 0; j < comp_names.size(); ++j) {
		const godot::String cname = comp_names[j];
		// add_component fills absent fields with their schema default (zero /
		// empty string / false / zeroed array slots) via component_dict_to_bytes.
		ent->add_component(cname, p_components[cname]);
	}
	return ent;
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

godot::Variant VECSWorld::get_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_default) {
	if (p_entity.is_valid()) {
		return p_entity->getf(p_comp, p_field, p_default);
	}
	return p_default;
}

void VECSWorld::set_field(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Variant &p_value) {
	if (p_entity.is_valid()) {
		p_entity->setf(p_comp, p_field, p_value);
	}
}

godot::Ref<VECSEntity> VECSWorld::find_by_components(const godot::Array &p_components) {
	godot::Ref<VECSQueryBuilder> q = query();
	return q->with_all(p_components)->execute_one();
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

VECSObserver *VECSWorld::create_observer(const godot::Callable &p_callable, const godot::Dictionary &p_opts) {
	VECSObserver *obs = memnew(VECSObserver);
	obs->set_callback(p_callable);

	if (p_opts.has("events")) {
		const godot::Variant ev = p_opts["events"];
		if (ev.get_type() == godot::Variant::INT) {
			obs->set_events(static_cast<int>(static_cast<int64_t>(ev)));
		} else if (ev.get_type() == godot::Variant::ARRAY) {
			const godot::Array names = ev;
			int mask = 0;
			for (int i = 0; i < names.size(); ++i) {
				const godot::String name = names[i];
				if (name == "added") {
					mask |= vortaris::EVENT_ADDED;
				} else if (name == "removed") {
					mask |= vortaris::EVENT_REMOVED;
				} else if (name == "changed") {
					mask |= vortaris::EVENT_CHANGED;
				} else if (name == "matched") {
					mask |= vortaris::EVENT_MATCHED;
				} else if (name == "unmatched") {
					mask |= vortaris::EVENT_UNMATCHED;
				} else if (name == "custom") {
					mask |= vortaris::EVENT_CUSTOM;
				}
			}
			obs->set_events(mask);
		}
	} else {
		obs->on_changed(); // default: CHANGED events
	}
	if (p_opts.has("components")) {
		obs->set_components(p_opts["components"]);
	}
	if (p_opts.has("fields")) {
		obs->set_fields(p_opts["fields"]);
	}
	if (p_opts.has("match_components")) {
		obs->set_match_components(p_opts["match_components"]);
	}
	if (p_opts.has("custom_event_name")) {
		obs->set_custom_event_name(p_opts["custom_event_name"]);
	}
	if (p_opts.has("throttle_tick")) {
		obs->set_throttle_tick(static_cast<int64_t>(p_opts["throttle_tick"]));
	}
	if (p_opts.has("flush_mode")) {
		obs->set_flush_mode(static_cast<int>(static_cast<int64_t>(p_opts["flush_mode"])));
	}
	add_observer(obs);
	return obs;
}

VECSObserver *VECSWorld::on_changed(const godot::String &p_comp, const godot::Dictionary &p_opts) {
	godot::Dictionary o;
	godot::Array events;
	events.append("changed");
	o["events"] = events;
	godot::Array comps;
	if (!p_comp.is_empty()) {
		comps.append(p_comp);
	}
	o["components"] = comps;
	// Accept both the singular CHANT spelling ("field") and the plural
	// ("fields") used by VECSObserver.set_fields.
	if (p_opts.has("fields")) {
		o["fields"] = p_opts["fields"];
	} else if (p_opts.has("field")) {
		o["fields"] = p_opts["field"];
	}
	if (p_opts.has("throttle_tick")) {
		o["throttle_tick"] = p_opts["throttle_tick"];
	}
	godot::Callable cb;
	if (p_opts.has("callable")) {
		cb = p_opts["callable"];
	}
	return create_observer(cb, o);
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

int64_t VECSWorld::emit_event(const godot::String &p_name, const godot::Ref<VECSEntity> &p_entity, const godot::Variant &p_payload) {
	vortaris::Entity e;
	if (p_entity.is_valid()) {
		e = p_entity->entity();
	}
	return static_cast<int64_t>(core_->emit_event(p_name, e, p_payload));
}

int64_t VECSWorld::on_field_changed(const godot::String &p_comp, const godot::String &p_field, const godot::Callable &p_callable) {
	vortaris::ComponentTypeId t = core_->registry().id_of(godot::StringName(p_comp));
	if (t == vortaris::INVALID_COMPONENT_TYPE || !p_callable.is_valid()) {
		return 0;
	}
	const int64_t sub_id = next_field_sub_id_++;
	FieldSubscription sub;
	sub.comp = t;
	sub.field = godot::StringName(p_field);
	sub.callable = p_callable;
	field_subs_[sub_id] = sub;

	vortaris::ObserverCallback cb;
	cb.event_mask = vortaris::EVENT_CHANGED;
	cb.component_filter = { t };
	cb.watch_all = false;
	cb.fn = [this, sub_id](vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
		auto it = field_subs_.find(sub_id);
		if (it == field_subs_.end()) {
			return;
		}
		FieldSubscription &sub = it->second;
		vortaris::World &w = core();
		const void *raw = w.get_raw(p_e, sub.comp);
		if (!raw) {
			return;
		}
		const vortaris::ComponentSchema *s = w.registry().schema_of(sub.comp);
		if (!s) {
			return;
		}
		const vortaris::FieldDescriptor *fd = s->find_field(sub.field);
		if (!fd) {
			return;
		}
		godot::Variant value;
		if (!vortaris::field_to_variant(*fd, static_cast<const uint8_t *>(raw) + fd->offset, value)) {
			return;
		}
		auto cit = sub.cached.find(p_e.id);
		if (cit != sub.cached.end() && cit->second == value) {
			return; // value unchanged since last delivery
		}
		sub.cached[p_e.id] = value;
		sub.callable.call(VECSEntity::make(&w, p_e), value);
	};
	field_subs_[sub_id].observer_id = core_->observer_dispatch().add(std::move(cb));
	return sub_id;
}

void VECSWorld::off(int64_t p_subscription_id) {
	auto it = field_subs_.find(p_subscription_id);
	if (it == field_subs_.end()) {
		return;
	}
	if (it->second.observer_id > 0) {
		core_->observer_dispatch().remove(it->second.observer_id);
	}
	field_subs_.erase(it);
}

void VECSWorld::_clear_field_subs() {
	for (auto &kv : field_subs_) {
		if (kv.second.observer_id > 0) {
			core_->observer_dispatch().remove(kv.second.observer_id);
		}
	}
	field_subs_.clear();
}

int64_t VECSWorld::subscribe_event(const godot::String &p_name, const godot::Callable &p_callable) {
	if (!p_callable.is_valid()) {
		return 0;
	}
	const int64_t sub_id = next_event_sub_id_++;
	vortaris::ObserverCallback cb;
	cb.event_mask = vortaris::EVENT_CUSTOM;
	cb.custom_name = p_name;
	cb.fn = [this, p_callable](vortaris::ObserverEventType p_type, vortaris::Entity p_e, vortaris::ComponentTypeId p_comp, const godot::String &p_name, const godot::Variant &p_payload) {
		VECSWorld *world = this;
		godot::Ref<VECSEntity> handle = p_e ? VECSEntity::make(&world->core(), p_e) : godot::Ref<VECSEntity>();
		p_callable.call(handle, p_payload);
	};
	event_subs_[sub_id] = core_->observer_dispatch().add(std::move(cb));
	return sub_id;
}

void VECSWorld::unsubscribe_event(int64_t p_subscription_id) {
	auto it = event_subs_.find(p_subscription_id);
	if (it == event_subs_.end()) {
		return;
	}
	core_->observer_dispatch().remove(it->second);
	event_subs_.erase(it);
}

void VECSWorld::_clear_event_subs() {
	for (auto &kv : event_subs_) {
		core_->observer_dispatch().remove(kv.second);
	}
	event_subs_.clear();
}

godot::Dictionary VECSWorld::copy_entity_to(const godot::Ref<VECSEntity> &p_entity, VECSWorld *p_target) {
	godot::Dictionary result;
	if (!p_entity.is_valid() || !p_target) {
		return result;
	}
	vortaris::Entity src = p_entity->entity();
	// The source entity lives in its OWN world, which may differ from this one.
	vortaris::World *swp = p_entity->world();
	if (!swp || !swp->is_alive(src)) {
		return result;
	}
	vortaris::World &sw = *swp;
	vortaris::World &tw = p_target->core();

	// Snapshot the source component data first (source is read-only).
	struct CompData {
		vortaris::ComponentTypeId type = 0;
		std::vector<uint8_t> bytes;
	};
	std::vector<CompData> comps;
	std::vector<vortaris::ComponentTypeId> types;
	sw.get_entity_component_types(src, types);
	for (vortaris::ComponentTypeId t : types) {
		const vortaris::ComponentSchema *s = sw.registry().schema_of(t);
		const void *raw = sw.get_raw(src, t);
		if (!s || !raw) {
			continue;
		}
		CompData cd;
		cd.type = t;
		cd.bytes.assign(static_cast<const uint8_t *>(raw), static_cast<const uint8_t *>(raw) + s->size);
		comps.push_back(std::move(cd));
	}

	vortaris::Entity target_e;
	if (p_target == this) {
		// Same-world clone: the source id is occupied, so a fresh id is used.
		target_e = tw.create_entity();
	} else {
		target_e = tw.create_entity_preassigned(src.id);
		if (!target_e) {
			target_e = tw.create_entity();
		}
	}
	if (!target_e) {
		return result;
	}

	if (p_target == this) {
		// Buffered through the command buffer, then committed once.
		for (const CompData &cd : comps) {
			tw.commands().add_component(target_e, cd.type, cd.bytes.data(), cd.bytes.size());
		}
		tw.flush_command_buffers();
	} else {
		for (const CompData &cd : comps) {
			tw.add_raw(target_e, cd.type, cd.bytes.data());
		}
	}

	result[static_cast<int64_t>(src.id)] = static_cast<int64_t>(target_e.id);
	return result;
}

godot::Dictionary VECSWorld::merge_world(VECSWorld *p_source) {
	godot::Dictionary total;
	if (!p_source) {
		return total;
	}
	// Snapshot the source entity ids first so that cloning into the same world
	// does not disturb the iteration.
	std::vector<vortaris::Entity> ids;
	for (const vortaris::Archetype *a : p_source->core().all_archetypes()) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			ids.push_back(a->entities[row]);
		}
	}
	for (vortaris::Entity e : ids) {
		godot::Ref<VECSEntity> handle = VECSEntity::make(&p_source->core(), e);
		const godot::Dictionary m = copy_entity_to(handle, this);
		const godot::Array keys = m.keys();
		for (int i = 0; i < keys.size(); ++i) {
			total[keys[i]] = m[keys[i]];
		}
	}
	return total;
}

godot::Dictionary VECSWorld::get_debug_stats() const {
	godot::Dictionary out;
	out["entity_count"] = static_cast<int64_t>(core_->entity_count());
	out["archetype_count"] = static_cast<int64_t>(core_->all_archetypes().size());
	out["component_count"] = static_cast<int64_t>(core_->registry().count());
	out["observer_count"] = static_cast<int64_t>(core_->observer_dispatch().count());
	out["change_tick"] = static_cast<int64_t>(core_->change_tick());
	out["pool_size"] = static_cast<int64_t>(core_->pool_size());
	out["query_cache_entries"] = static_cast<int64_t>(core_->query_cache().cached_query_count());
	return out;
}

namespace {
const char *debug_field_type_name(vortaris::FieldType p_t) {
	switch (p_t) {
		case vortaris::FieldType::Bool: return "Bool";
		case vortaris::FieldType::I8: return "I8";
		case vortaris::FieldType::I16: return "I16";
		case vortaris::FieldType::I32: return "I32";
		case vortaris::FieldType::I64: return "I64";
		case vortaris::FieldType::U8: return "U8";
		case vortaris::FieldType::U16: return "U16";
		case vortaris::FieldType::U32: return "U32";
		case vortaris::FieldType::U64: return "U64";
		case vortaris::FieldType::F32: return "F32";
		case vortaris::FieldType::F64: return "F64";
		case vortaris::FieldType::Vector2: return "Vector2";
		case vortaris::FieldType::Vector2i: return "Vector2i";
		case vortaris::FieldType::Vector3: return "Vector3";
		case vortaris::FieldType::Vector3i: return "Vector3i";
		case vortaris::FieldType::Vector4: return "Vector4";
		case vortaris::FieldType::Vector4i: return "Vector4i";
		case vortaris::FieldType::Color: return "Color";
		case vortaris::FieldType::Quaternion: return "Quaternion";
		case vortaris::FieldType::Basis: return "Basis";
		case vortaris::FieldType::Transform2D: return "Transform2D";
		case vortaris::FieldType::Transform3D: return "Transform3D";
		case vortaris::FieldType::AABB: return "AABB";
		case vortaris::FieldType::Rect2: return "Rect2";
		case vortaris::FieldType::Plane: return "Plane";
		case vortaris::FieldType::StringFixed: return "StringFixed";
		case vortaris::FieldType::Blob: return "Blob";
	}
	return "";
}

// Maps a field descriptor to the Variant type the debugger channel requires for
// a value. Fixed-array fields (count > 1) are Arrays; StringFixed/Blob map to
// String / PackedByteArray — their `count` is a byte-buffer capacity, NOT an
// array length. Returns NIL for unknown FieldTypes (callers skip the check then,
// letting the schema conversion handle it).
godot::Variant::Type debug_expected_variant_type(const vortaris::FieldDescriptor &p_fd) {
	if (!p_fd.is_scalar()) {
		return p_fd.type == vortaris::FieldType::Blob ? godot::Variant::PACKED_BYTE_ARRAY : godot::Variant::STRING;
	}
	if (p_fd.count > 1) {
		return godot::Variant::ARRAY;
	}
	switch (p_fd.type) {
		case vortaris::FieldType::Bool: return godot::Variant::BOOL;
		case vortaris::FieldType::I8:
		case vortaris::FieldType::I16:
		case vortaris::FieldType::I32:
		case vortaris::FieldType::I64:
		case vortaris::FieldType::U8:
		case vortaris::FieldType::U16:
		case vortaris::FieldType::U32:
		case vortaris::FieldType::U64: return godot::Variant::INT;
		case vortaris::FieldType::F32:
		case vortaris::FieldType::F64: return godot::Variant::FLOAT;
		case vortaris::FieldType::Vector2: return godot::Variant::VECTOR2;
		case vortaris::FieldType::Vector2i: return godot::Variant::VECTOR2I;
		case vortaris::FieldType::Vector3: return godot::Variant::VECTOR3;
		case vortaris::FieldType::Vector3i: return godot::Variant::VECTOR3I;
		case vortaris::FieldType::Vector4: return godot::Variant::VECTOR4;
		case vortaris::FieldType::Vector4i: return godot::Variant::VECTOR4I;
		case vortaris::FieldType::Color: return godot::Variant::COLOR;
		case vortaris::FieldType::Quaternion: return godot::Variant::QUATERNION;
		case vortaris::FieldType::Basis: return godot::Variant::BASIS;
		case vortaris::FieldType::Transform2D: return godot::Variant::TRANSFORM2D;
		case vortaris::FieldType::Transform3D: return godot::Variant::TRANSFORM3D;
		case vortaris::FieldType::AABB: return godot::Variant::AABB;
		case vortaris::FieldType::Rect2: return godot::Variant::RECT2;
		case vortaris::FieldType::Plane: return godot::Variant::PLANE;
	}
	return godot::Variant::NIL;
}
} // namespace

godot::Dictionary VECSWorld::get_snapshot_data() {
	godot::Dictionary out;
	out["protocol"] = static_cast<int64_t>(1);
	out["version"] = static_cast<int64_t>(vortaris::SNAPSHOT_VERSION);
	out["stats"] = get_debug_stats();

	// Registered component types (the process-global registry).
	godot::Array comps;
	const vortaris::ComponentRegistry &reg = core_->registry();
	for (size_t i = 0; i < reg.count(); ++i) {
		const vortaris::ComponentSchema *s = reg.schema_of(static_cast<vortaris::ComponentTypeId>(i + 1));
		if (!s) {
			continue;
		}
		godot::Dictionary cd;
		cd["name"] = godot::String(s->type_name);
		cd["id"] = static_cast<int64_t>(s->type_id);
		cd["size"] = static_cast<int64_t>(s->size);
		godot::Array fields;
		for (const vortaris::FieldDescriptor &f : s->fields) {
			godot::Dictionary fd;
			fd["name"] = godot::String(f.name);
			fd["type"] = godot::String(debug_field_type_name(f.type));
			fd["count"] = static_cast<int64_t>(f.count);
			fd["sync_priority"] = static_cast<int64_t>(f.sync_priority);
			fd["networked"] = f.is_networked;
			fields.append(fd);
		}
		cd["fields"] = fields;
		comps.append(cd);
	}
	out["components"] = comps;

	// Registered systems (name / group / enabled flags).
	godot::Array systems;
	std::vector<VECSSystem *> syslist;
	scheduler_->collect_systems(syslist);
	for (VECSSystem *s : syslist) {
		godot::Dictionary sd;
		sd["name"] = s->get_system_name();
		sd["group"] = s->get_group();
		sd["active"] = s->get_active();
		sd["paused"] = s->get_paused();
		sd["tick_interval"] = s->get_tick_interval();
		sd["flush_mode"] = static_cast<int64_t>(s->get_flush_mode());
		systems.append(sd);
	}
	out["systems"] = systems;

	// The remote-monitor entity table is bounded by
	// `vortarisecs/general/max_snapshot_entities` (default 500) so a huge world
	// (100k+ entities) does not serialize megabytes every auto-refresh tick.
	// Save-file serialization (serialize_snapshot_json) still exports everything.
	const int64_t max_entities = vortaris::get_max_snapshot_entities();
	const int64_t total_entities = static_cast<int64_t>(core_->entity_count());
	const godot::Array ents = entities_to_data(max_entities);
	out["entities"] = ents;
	if (max_entities > 0 && total_entities > static_cast<int64_t>(ents.size())) {
		out["truncated"] = true;
		out["entity_total"] = total_entities;
	}
	return out;
}

bool VECSWorld::_debugger_capture(const godot::String &p_message, const godot::Variant &p_data) {
	if (p_message == "req_snapshot") {
		// Send only while a debugger is actually connected: `send_message` on an
		// inactive EngineDebugger is a no-op anyway, but guarding on is_active()
		// also keeps the snapshot computation (which walks every entity) off the
		// hot path when nobody is listening.
		godot::EngineDebugger *dbg = godot::EngineDebugger::get_singleton();
		if (dbg && dbg->is_active()) {
			godot::Array payload;
			payload.append(get_snapshot_data());
			dbg->send_message("vecs:snapshot", payload);
		}
		return true;
	}
	if (p_message == "set_field") {
		_handle_debug_set_field(p_data);
		return true;
	}
	return false; // not ours — let the debugger try other captures
}

godot::Dictionary VECSWorld::debug_set_field(int64_t p_entity_id, const godot::String &p_comp,
		const godot::String &p_field, const godot::Variant &p_value) {
	godot::Dictionary out;
	out["ok"] = false;
	out["error"] = godot::String();
	if (p_entity_id <= 0) {
		out["error"] = "invalid entity id " + godot::String::num_int64(p_entity_id);
		return out;
	}
	const vortaris::Entity e{ static_cast<uint64_t>(p_entity_id) };
	if (!core_->is_alive(e)) {
		out["error"] = "entity " + godot::String::num_int64(p_entity_id) + " is not alive";
		return out;
	}
	const vortaris::ComponentTypeId tid = core_->registry().id_of(godot::StringName(p_comp));
	if (tid == vortaris::INVALID_COMPONENT_TYPE) {
		out["error"] = "component '" + p_comp + "' is not registered";
		return out;
	}
	const vortaris::ComponentSchema *schema = core_->registry().schema_of(tid);
	const vortaris::FieldDescriptor *fd = schema ? schema->find_field(godot::StringName(p_field)) : nullptr;
	if (!fd) {
		out["error"] = "field '" + p_field + "' not found on component '" + p_comp + "'";
		return out;
	}
	void *raw = core_->get_raw(e, tid);
	if (!raw) {
		out["error"] = "entity does not carry component '" + p_comp + "'";
		return out;
	}
	// Type-check before writing: godot-cpp's implicit Variant->T conversion
	// silently coerces incompatible values (a Vector3 passed to an F32 field
	// becomes 0), so without this a bad edit would "succeed" and zero the field.
	// Reject any Variant whose type does not match the field's expected type.
	const godot::Variant::Type expected_type = debug_expected_variant_type(*fd);
	if (expected_type != godot::Variant::NIL && p_value.get_type() != expected_type) {
		out["error"] = "type mismatch for field '" + p_field + "' (expected " +
				godot::String(godot::Variant::get_type_name(expected_type)) + ", got " +
				godot::String(godot::Variant::get_type_name(p_value.get_type())) + ")";
		return out;
	}
	// Apply through the same schema-driven conversion as VECSComponent::set_field.
	if (!vortaris::field_from_variant(*fd, static_cast<uint8_t *>(raw) + fd->offset, p_value)) {
		out["error"] = "incompatible value for field '" + p_field + "'";
		return out;
	}
	// Read back through the snapshot path so the write is verified observable
	// (defense in depth against a conversion that silently mis-writes).
	godot::Variant written;
	if (!vortaris::field_to_variant(*fd, static_cast<const uint8_t *>(raw) + fd->offset, written) ||
			written.get_type() != expected_type) {
		out["error"] = "field '" + p_field + "' did not verify after write";
		return out;
	}
	core_->mark_changed(e, tid, p_field);
	out["ok"] = true;
	return out;
}

void VECSWorld::_handle_debug_set_field(const godot::Variant &p_data) {
	int64_t entity_id = 0;
	godot::String comp;
	godot::String field;
	godot::Variant value;
	bool parsed = false;
	if (p_data.get_type() == godot::Variant::ARRAY) {
		const godot::Array args = p_data;
		if (args.size() >= 4) {
			entity_id = static_cast<int64_t>(args[0]);
			comp = args[1];
			field = args[2];
			value = args[3];
			parsed = true;
		}
	}
	godot::Dictionary result;
	if (parsed) {
		result = debug_set_field(entity_id, comp, field, value);
	} else {
		result["ok"] = false;
		result["error"] = "set_field expects [entity_id, comp, field, value]";
	}
	const bool ok = result["ok"];
	const godot::String err = result["error"];
	if (!ok) {
		vortaris::log_debug("remote set_field rejected: " + err);
	}
	// Ack is best-effort: only sent when a debugger is actually connected.
	godot::EngineDebugger *dbg = godot::EngineDebugger::get_singleton();
	if (dbg && dbg->is_active()) {
		godot::Array ack;
		ack.append(ok);
		ack.append(entity_id);
		ack.append(comp);
		ack.append(field);
		ack.append(err);
		dbg->send_message("vecs:set_field_result", ack);
	}
}

void VECSWorld::set_verbose(bool p_on) {
	vortaris::set_verbose(p_on);
}

bool VECSWorld::is_verbose() const {
#ifdef DEBUG_ENABLED
	// Re-read the persisted setting (canonical path, legacy fallback) into the
	// logging cache so the reported state always agrees with what log_verbose()
	// actually emits — including a direct ProjectSettings write that bypasses
	// set_verbose() (E3). Release builds compile verbose logging out entirely.
	vortaris::refresh_verbose();
	return vortaris::verbose_active();
#else
	return false;
#endif
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
	_clear_field_subs();
	_clear_event_subs();
	core_->reset();
	scheduler_->clear();
}

VECSWorld *VECSWorld::get_world() {
	return this;
}

godot::PackedByteArray VECSWorld::serialize_snapshot() const {
	vortaris::BinaryBuffer buf;
	core_->serialize_snapshot(buf);
	vortaris::log_verbose("snapshot serialized (" + godot::String::num_int64(static_cast<int64_t>(buf.size())) + " bytes)");
	return buf.to_packed();
}

bool VECSWorld::deserialize_snapshot(const godot::PackedByteArray &p_data) {
	vortaris::BinaryBuffer buf;
	buf.from_packed(p_data);
	const bool ok = core_->deserialize_snapshot(buf);
	vortaris::log_verbose("snapshot deserialized (" + godot::String::num_int64(static_cast<int64_t>(p_data.size())) + " bytes, ok=" + (ok ? godot::String("true") : godot::String("false")) + ")");
	return ok;
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
	vortaris::log_verbose("batch-registered " + godot::String::num_int64(p_components.size()) + " components");
	return true;
}

godot::Array VECSWorld::spawn_from_data(const godot::Array &p_entities) {
	godot::Dictionary ignored;
	return _spawn_from_data_impl(p_entities, ignored);
}

godot::Dictionary VECSWorld::spawn_from_data_mapped(const godot::Array &p_entities) {
	godot::Dictionary mapping;
	_spawn_from_data_impl(p_entities, mapping);
	return mapping;
}

godot::Array VECSWorld::_spawn_from_data_impl(const godot::Array &p_entities, godot::Dictionary &r_mapping) {
	godot::Array out;
	for (int i = 0; i < p_entities.size(); ++i) {
		if (p_entities[i].get_type() != godot::Variant::DICTIONARY) {
			ERR_PRINT("VortarisECS: spawn_from_data entry must be a Dictionary.");
			continue;
		}
		const godot::Dictionary edata = p_entities[i];
		godot::Ref<VECSEntity> ent;
		const bool has_id = edata.has("id");
		if (has_id) {
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
		// Map the source key (explicit id, or array index) to the new entity id.
		const int64_t new_id = static_cast<int64_t>(ent->entity().id);
		if (has_id) {
			r_mapping[edata["id"]] = new_id;
		} else {
			r_mapping[static_cast<int64_t>(i)] = new_id;
		}
		out.append(ent);
	}

	// Auto-parenting: an entry may carry a "parent" key whose value is the source
	// id or array index of another entity in the same batch. After the whole
	// batch is spawned (so the mapping is complete), the child's "parent" field
	// (the field literally named "parent" on one of its components) is rewritten
	// from the source key to the freshly assigned parent id.
	for (int i = 0; i < p_entities.size() && i < out.size(); ++i) {
		if (p_entities[i].get_type() != godot::Variant::DICTIONARY) {
			continue;
		}
		const godot::Dictionary edata = p_entities[i];
		if (!edata.has("parent")) {
			continue;
		}
		const godot::Variant parent_src = edata["parent"];
		if (!r_mapping.has(parent_src)) {
			continue;
		}
		godot::Ref<VECSEntity> child = out[i];
		vortaris::World &w = core();
		std::vector<vortaris::ComponentTypeId> types;
		w.get_entity_component_types(child->entity(), types);
		for (vortaris::ComponentTypeId t : types) {
			const vortaris::ComponentSchema *s = w.registry().schema_of(t);
			if (!s || !s->find_field(godot::StringName("parent"))) {
				continue;
			}
			remap_reference(child, godot::String(s->type_name), godot::String("parent"), r_mapping);
			break;
		}
	}
	return out;
}

godot::Array VECSWorld::entities_to_data(int64_t p_max_entities) {
	godot::Array out;
	vortaris::World &w = core();
	std::vector<vortaris::Archetype *> arches = w.all_archetypes();
	std::sort(arches.begin(), arches.end(), [](const vortaris::Archetype *a, const vortaris::Archetype *b) {
		return a->signature < b->signature;
	});
	int64_t emitted = 0;
	for (const vortaris::Archetype *a : arches) {
		for (size_t row = 0; row < a->entities.size(); ++row) {
			// Bounded export (E1): stop early instead of walking the rest of a
			// huge world when the caller passed a cap (the remote monitor does).
			if (p_max_entities > 0 && emitted >= p_max_entities) {
				return out;
			}
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
			++emitted;
		}
	}
	return out;
}

godot::Dictionary VECSWorld::serialize_snapshot_json() {
	godot::Dictionary out;
	out["version"] = static_cast<int64_t>(vortaris::SNAPSHOT_VERSION);
	out["entities"] = entities_to_data();
	const godot::Array ents = out["entities"];
	vortaris::log_verbose("snapshot JSON serialized (" + godot::String::num_int64(ents.size()) + " entities)");
	return out;
}

godot::String VECSWorld::serialize_snapshot_json_string() {
	// Honors `vortarisecs/serialization/compact_json`: true => unindented
	// (compact) JSON, false => pretty-printed with tab indentation.
	const bool compact = vortaris::get_compact_json();
	return godot::JSON::stringify(serialize_snapshot_json(), compact ? "" : "\t");
}

namespace {
bool parse_snapshot_root(const godot::Variant &p_data, godot::Dictionary &r_root) {
	if (p_data.get_type() == godot::Variant::STRING) {
		const godot::Variant parsed = godot::JSON::parse_string(p_data);
		if (parsed.get_type() != godot::Variant::DICTIONARY) {
			ERR_PRINT("VortarisECS: invalid JSON save data.");
			return false;
		}
		r_root = parsed;
	} else if (p_data.get_type() == godot::Variant::DICTIONARY) {
		r_root = p_data;
	} else {
		ERR_PRINT("VortarisECS: deserialize_snapshot_json expects a Dictionary or a JSON String.");
		return false;
	}
	return true;
}
} // namespace

bool VECSWorld::deserialize_snapshot_json(const godot::Variant &p_data) {
	godot::Dictionary root;
	if (!parse_snapshot_root(p_data, root)) {
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
	// Loading a save replaces the world, but a partial/failed load must not
	// destroy the live world: back it up first so it can be restored.
	const godot::String backup = serialize_snapshot_json_string();
	// Loading a save replaces the world: drop any existing entities first.
	core_->clear();
	const godot::Array spawned = spawn_from_data(ents);
	if (spawned.size() != ents.size()) {
		ERR_PRINT("VortarisECS: some entities failed to deserialize; restoring the previous world.");
		core_->clear();
		deserialize_snapshot_json(backup);
		return false;
	}
	vortaris::log_verbose("snapshot JSON loaded (" + godot::String::num_int64(spawned.size()) + " entities)");
	return true;
}

godot::Dictionary VECSWorld::deserialize_snapshot_json_mapped(const godot::Variant &p_data) {
	godot::Dictionary root;
	if (!parse_snapshot_root(p_data, root)) {
		return godot::Dictionary();
	}
	if (root.has("version")) {
		const int64_t v = root["version"];
		if (v != static_cast<int64_t>(vortaris::SNAPSHOT_VERSION)) {
			ERR_PRINT("VortarisECS: save version mismatch (got " + godot::String::num_int64(v) + ", expected " + godot::String::num_int64(vortaris::SNAPSHOT_VERSION) + ").");
			return godot::Dictionary();
		}
	}
	if (!root.has("entities")) {
		ERR_PRINT("VortarisECS: save data has no 'entities'.");
		return godot::Dictionary();
	}
	const godot::Array ents = root["entities"];
	// Same backup-before-replace contract as deserialize_snapshot_json: a
	// partial/failed load restores the previous world instead of leaving it
	// cleared and half-rebuilt.
	const godot::String backup = serialize_snapshot_json_string();
	core_->clear();
	godot::Dictionary mapping = spawn_from_data_mapped(ents);
	if (mapping.size() != ents.size()) {
		ERR_PRINT("VortarisECS: some entities failed to deserialize; restoring the previous world.");
		core_->clear();
		deserialize_snapshot_json(backup);
		return godot::Dictionary();
	}
	vortaris::log_verbose("snapshot JSON loaded with id mapping (" + godot::String::num_int64(mapping.size()) + " entities)");
	return mapping;
}

void VECSWorld::remap_reference(const godot::Ref<VECSEntity> &p_entity, const godot::String &p_comp, const godot::String &p_field, const godot::Dictionary &p_map) {
	if (!p_entity.is_valid()) {
		return;
	}
	const godot::Variant current = p_entity->getf(p_comp, p_field, godot::Variant());
	if (current.get_type() != godot::Variant::INT && current.get_type() != godot::Variant::FLOAT) {
		return; // not a numeric entity-reference field
	}
	const int64_t src = static_cast<int64_t>(current);
	if (!p_map.has(src)) {
		return; // no mapping for this source id
	}
	const int64_t dst = p_map[src];
	p_entity->setf(p_comp, p_field, dst);
}

void VECSWorld::_bind_methods() {
	using namespace godot;
	ClassDB::bind_method(D_METHOD("create_entity"), &VECSWorld::create_entity);
	ClassDB::bind_method(D_METHOD("create_entity_preassigned", "id"), &VECSWorld::create_entity_preassigned);
	ClassDB::bind_method(D_METHOD("create_entity_pooled"), &VECSWorld::create_entity_pooled);
	ClassDB::bind_method(D_METHOD("destroy_entity_pooled", "entity"), &VECSWorld::destroy_entity_pooled);
	ClassDB::bind_method(D_METHOD("pool_size"), &VECSWorld::pool_size);
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
	ClassDB::bind_method(D_METHOD("create_with_components", "def_id", "components"), &VECSWorld::create_with_components);
	ClassDB::bind_method(D_METHOD("each", "components", "callable"), &VECSWorld::each);
	ClassDB::bind_method(D_METHOD("get_field", "entity", "comp", "field", "default"), &VECSWorld::get_field, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("set_field", "entity", "comp", "field", "value"), &VECSWorld::set_field);
	ClassDB::bind_method(D_METHOD("find_by_components", "components"), &VECSWorld::find_by_components);
	ClassDB::bind_method(D_METHOD("add_system", "system"), &VECSWorld::add_system);
	ClassDB::bind_method(D_METHOD("remove_system", "system"), &VECSWorld::remove_system);
	ClassDB::bind_method(D_METHOD("system_count"), &VECSWorld::system_count);
	ClassDB::bind_method(D_METHOD("add_observer", "observer"), &VECSWorld::add_observer);
	ClassDB::bind_method(D_METHOD("remove_observer", "observer"), &VECSWorld::remove_observer);
	ClassDB::bind_method(D_METHOD("create_observer", "callable", "opts"), &VECSWorld::create_observer, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("on_changed", "comp", "opts"), &VECSWorld::on_changed);
	ClassDB::bind_method(D_METHOD("emit_event", "name", "entity", "payload"), &VECSWorld::emit_event, DEFVAL(Variant()));
	ClassDB::bind_method(D_METHOD("on_field_changed", "comp", "field", "callable"), &VECSWorld::on_field_changed);
	ClassDB::bind_method(D_METHOD("off", "subscription_id"), &VECSWorld::off);
	ClassDB::bind_method(D_METHOD("subscribe_event", "name", "callable"), &VECSWorld::subscribe_event);
	ClassDB::bind_method(D_METHOD("unsubscribe_event", "subscription_id"), &VECSWorld::unsubscribe_event);
	ClassDB::bind_method(D_METHOD("copy_entity_to", "entity", "target"), &VECSWorld::copy_entity_to);
	ClassDB::bind_method(D_METHOD("merge_world", "source"), &VECSWorld::merge_world);
	ClassDB::bind_method(D_METHOD("get_debug_stats"), &VECSWorld::get_debug_stats);
	ClassDB::bind_method(D_METHOD("get_snapshot_data"), &VECSWorld::get_snapshot_data);
	ClassDB::bind_method(D_METHOD("debug_set_field", "entity_id", "comp", "field", "value"), &VECSWorld::debug_set_field);
	ClassDB::bind_method(D_METHOD("set_verbose", "on"), &VECSWorld::set_verbose);
	ClassDB::bind_method(D_METHOD("is_verbose"), &VECSWorld::is_verbose);
	ClassDB::bind_method(D_METHOD("process", "delta", "group"), &VECSWorld::process, DEFVAL(""));
	ClassDB::bind_method(D_METHOD("compact"), &VECSWorld::compact);
	ClassDB::bind_method(D_METHOD("shutdown"), &VECSWorld::shutdown);
	ClassDB::bind_method(D_METHOD("get_world"), &VECSWorld::get_world);
	ClassDB::bind_method(D_METHOD("serialize_snapshot"), &VECSWorld::serialize_snapshot);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot", "data"), &VECSWorld::deserialize_snapshot);
	ClassDB::bind_method(D_METHOD("register_components", "components"), &VECSWorld::register_components);
	ClassDB::bind_method(D_METHOD("spawn_from_data", "entities"), &VECSWorld::spawn_from_data);
	ClassDB::bind_method(D_METHOD("spawn_from_data_mapped", "entities"), &VECSWorld::spawn_from_data_mapped);
	ClassDB::bind_method(D_METHOD("entities_to_data", "max_entities"), &VECSWorld::entities_to_data, DEFVAL((int64_t)0));
	ClassDB::bind_method(D_METHOD("serialize_snapshot_json"), &VECSWorld::serialize_snapshot_json);
	ClassDB::bind_method(D_METHOD("serialize_snapshot_json_string"), &VECSWorld::serialize_snapshot_json_string);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot_json", "data"), &VECSWorld::deserialize_snapshot_json);
	ClassDB::bind_method(D_METHOD("deserialize_snapshot_json_mapped", "data"), &VECSWorld::deserialize_snapshot_json_mapped);
	ClassDB::bind_method(D_METHOD("remap_reference", "entity", "comp", "field", "map"), &VECSWorld::remap_reference);
}
