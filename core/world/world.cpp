#include "world.h"

#include "component.h"

#include <framework/assert.h>
#include <main/class_db.h>

#include <algorithm>
#include <deque>
#include <format>

namespace feather {

namespace {

// A class name reaches ClassInfo as a view, and flecs keeps the name it is given, so both need somewhere permanent to point.
// Only ever added to, so a pointer handed out earlier stays good.
std::string_view intern(const std::string& name) {
	static auto* names = new std::deque<std::string>();
	for (const std::string& existing : *names) {
		if (existing == name) {
			return existing;
		}
	}
	names->push_back(name);
	return names->back();
}

size_t align_up(size_t offset, size_t alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}

// flecs calls hooks per run of elements; ValueTypeOps is spelled the same way,
// so each one forwards directly rather than looping here.
void hook_ctor(void* ptr, int32_t count, const ecs_type_info_t* info) {
	const auto* ops = static_cast<const ValueTypeOps*>(info->hooks.binding_ctx);
	ops->default_construct(ptr, static_cast<size_t>(count));
}

void hook_dtor(void* ptr, int32_t count, const ecs_type_info_t* info) {
	const auto* ops = static_cast<const ValueTypeOps*>(info->hooks.binding_ctx);
	ops->destruct(ptr, static_cast<size_t>(count));
}

void hook_copy(void* dst, const void* src, int32_t count, const ecs_type_info_t* info) {
	const auto* ops = static_cast<const ValueTypeOps*>(info->hooks.binding_ctx);
	ops->copy(dst, src, static_cast<size_t>(count));
}

void hook_move(void* dst, void* src, int32_t count, const ecs_type_info_t* info) {
	const auto* ops = static_cast<const ValueTypeOps*>(info->hooks.binding_ctx);
	ops->move(dst, src, static_cast<size_t>(count));
}

// The ops a component's hooks read at runtime, kept alive for the process. A component cannot be withdrawn from a world that may already
// store it, so these are only ever added to.
const ValueTypeOps* keep(const ValueTypeOps& ops) {
	static auto* kept = new std::deque<ValueTypeOps>();
	kept->push_back(ops);
	return &kept->back();
}

const ClassInfo::Property* find_property(const ClassInfo& info, std::string_view name) {
	for (const ClassInfo::Property& property : info.properties) {
		if (property.name == name) {
			return &property;
		}
	}
	return nullptr;
}

} //namespace

World::World() : _ecs(std::make_unique<Ecs::world>()) {
	_watch_component_registrations();
}

World::~World() {
	if (_component_delegate != static_cast<Delegate<std::string_view>::id_t>(-1)) {
		ClassDB::unregister_subclass_delegate(Component::get_class_static(), _component_delegate);
	}
}

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

void World::_watch_component_registrations() {
	// Everything already registered, then everything that registers later. Both halves matter: core's components are in ClassDB before any
	// world exists, while an extension's arrive when it loads, long after.
	for (StaticString name : ClassDB::get_children_names(Component::get_class_static())) {
		register_component(name);
	}

	_component_delegate = ClassDB::on_subclass_registered(
			Component::get_class_static(),
			[this](std::string_view class_name) { register_component(StaticString(class_name)); }
	);
}

bool World::progress(double delta) {
	return _ecs->progress(static_cast<float>(delta));
}

Entity World::create_entity(const std::string& name) const {
	return name.empty() ? _ecs->entity() : _ecs->entity(name.c_str());
}

Entity World::create_entity(const Entity& parent, const std::string& name) const {
	return create_entity(name).child_of(parent);
}

Entity World::entity(Ecs::entity_t id) const {
	return _ecs->entity(id);
}

Entity World::prefab(const std::string& name) const {
	return _ecs->prefab(name.c_str());
}

Entity World::lookup(const std::string& name) const {
	return _ecs->lookup(name.c_str());
}

void World::destroy_entity(Ecs::entity_t id) const {
	_ecs->entity(id).destruct();
}

bool World::is_valid(Ecs::entity_t id) const {
	return _ecs->entity(id).is_valid();
}

Ecs::entity_t World::register_component(StaticString class_name) {
	if (auto it = _components.find(class_name); it != _components.end()) {
		return it->second;
	}

	const ClassInfo* info = ClassDB::get_class_info(class_name);
	if (!info || !info->is_value_type) {
		// A Component subclass is always a value type; anything else naming
		// itself one is a mistake worth reporting rather than registering.
		return 0;
	}

	// Component itself is the empty base every component shares. It marks the
	// family rather than describing storage, so it is never registered.
	if (class_name == Component::get_class_static()) {
		return 0;
	}

	const ValueTypeOps& ops = info->value_ops;
	if (ops.size == 0) {
		// A zero-size component is a flecs tag, which has no storage to get or
		// set. Registered as one deliberately, not by accident.
		ecs_entity_desc_t tag_desc {};
		tag_desc.name = info->name.data();
		const Ecs::entity_t tag = ecs_entity_init(_ecs->c_ptr(), &tag_desc);
		_components[class_name] = tag;
		return tag;
	}

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = info->name.data();
	const Ecs::entity_t entity = ecs_entity_init(_ecs->c_ptr(), &entity_desc);
	if (!entity) {
		return 0;
	}

	ecs_component_desc_t desc {};
	desc.entity = entity;
	desc.type.size = static_cast<ecs_size_t>(ops.size);
	desc.type.alignment = static_cast<ecs_size_t>(ops.alignment);
	desc.type.name = info->name.data();

	// Only the hooks the type actually needs; a null one tells flecs the
	// operation is trivial and whole runs can be moved with memcpy.
	if (ops.default_construct || ops.destruct || ops.copy || ops.move) {
		const ValueTypeOps* kept_ops = keep(ops);
		desc.type.hooks.binding_ctx = const_cast<ValueTypeOps*>(kept_ops);
		desc.type.hooks.ctor = kept_ops->default_construct ? hook_ctor : flecs_default_ctor;
		desc.type.hooks.dtor = kept_ops->destruct ? hook_dtor : nullptr;
		desc.type.hooks.copy = kept_ops->copy ? hook_copy : nullptr;
		desc.type.hooks.move = kept_ops->move ? hook_move : nullptr;
	}
	else {
		// Zero-initialized on add, so a reader sees defined values before
		// anything has written. Every other operation is a memcpy.
		desc.type.hooks.ctor = flecs_default_ctor;
	}

	const Ecs::entity_t component = ecs_component_init(_ecs->c_ptr(), &desc);
	if (!component) {
		return 0;
	}

	_components[class_name] = component;
	return component;
}

Ecs::entity_t World::_begin_module(StaticString class_name, Entity& out_module) {
	// A module is an entity everything it declares is scoped under, which is how
	// flecs namespaces a module's components and systems.
	out_module = _ecs->entity(class_name.data());
	out_module.add(Ecs::Module);
	return ecs_set_scope(_ecs->c_ptr(), out_module.id());
}

void World::_end_module(Ecs::entity_t previous_scope) {
	ecs_set_scope(_ecs->c_ptr(), previous_scope);
}

bool World::is_module_imported(StaticString class_name) const {
	return _modules.contains(class_name);
}

Ecs::entity_t World::find_component(StaticString class_name) const {
	auto it = _components.find(class_name);
	return it == _components.end() ? 0 : it->second;
}

Ecs::entity_t World::register_described_component(
		const std::string& name,
		std::vector<ClassInfo::Property> properties,
		ValueTypeOps ops,
		std::string* error
) {
	auto fail = [&error](std::string message) -> Ecs::entity_t {
		if (error) {
			*error = std::move(message);
		}
		return 0;
	};

	if (name.empty()) {
		return fail("a component needs a name");
	}
	if (ops.size == 0) {
		return fail(std::format("component '{}' has no fields", name));
	}
	if (_ecs->lookup(name.c_str()).is_valid()) {
		return fail(std::format("'{}' already exists in the world", name));
	}

	const StaticString interned = StaticString(intern(name));

	// ClassDB first: it is the half that can refuse, and a refused registration
	// must not leave a component behind that nothing can read.
	if (!ClassDB::register_scripted_value_class(interned, std::move(properties))) {
		return fail(std::format("'{}' is already a registered class", name));
	}

	// The description carries its own storage, which register_scripted_value_class has no T to derive.
	if (ClassInfo* info = const_cast<ClassInfo*>(ClassDB::get_class_info(interned))) {
		info->value_ops = ops;
	}

	const Ecs::entity_t component = register_component(interned);
	if (!component) {
		return fail(std::format("the world rejected the layout for component '{}'", name));
	}
	return component;
}

bool World::add_component(Ecs::entity_t entity, StaticString class_name) {
	const Ecs::entity_t component = register_component(class_name);
	if (!component) {
		return false;
	}
	ecs_add_id(_ecs->c_ptr(), entity, component);
	return true;
}

bool World::has_component(Ecs::entity_t entity, StaticString class_name) const {
	const Ecs::entity_t component = find_component(class_name);
	return component != 0 && ecs_has_id(_ecs->c_ptr(), entity, component);
}

void World::remove_component(Ecs::entity_t entity, StaticString class_name) {
	if (const Ecs::entity_t component = find_component(class_name)) {
		ecs_remove_id(_ecs->c_ptr(), entity, component);
	}
}

const void* World::component_data(Ecs::entity_t entity, StaticString class_name) const {
	const Ecs::entity_t component = find_component(class_name);
	return component ? ecs_get_id(_ecs->c_ptr(), entity, component) : nullptr;
}

void* World::mutable_component_data(Ecs::entity_t entity, StaticString class_name) {
	const Ecs::entity_t component = register_component(class_name);
	if (!component) {
		return nullptr;
	}
	const ecs_type_info_t* type_info = ecs_get_type_info(_ecs->c_ptr(), component);
	if (!type_info || type_info->size <= 0) {
		// A tag has no storage to hand back.
		return nullptr;
	}
	void* data = ecs_ensure_id(_ecs->c_ptr(), entity, component, static_cast<size_t>(type_info->size));
	if (data) {
		ecs_modified_id(_ecs->c_ptr(), entity, component);
	}
	return data;
}

Variant World::get_property(Ecs::entity_t entity, StaticString class_name, std::string_view property) const {
	const ClassInfo* info = ClassDB::get_class_info(class_name);
	if (!info) {
		return {};
	}
	const ClassInfo::Property* found = find_property(*info, property);
	if (!found || !found->getter) {
		return {};
	}
	const void* data = component_data(entity, class_name);
	if (!data) {
		return {};
	}
	// A value type's accessors take the instance itself, not a Reflected*.
	return found->getter(const_cast<void*>(data));
}

bool World::set_property(
		Ecs::entity_t entity,
		StaticString class_name,
		std::string_view property,
		const Variant& value
) {
	const ClassInfo* info = ClassDB::get_class_info(class_name);
	if (!info) {
		return false;
	}
	const ClassInfo::Property* found = find_property(*info, property);
	if (!found || !found->setter) {
		return false;
	}
	void* data = mutable_component_data(entity, class_name);
	if (!data) {
		return false;
	}
	found->setter(data, value);
	return true;
}

} //namespace feather
