#pragma once

#include "ecs_defs.h"

#include <framework/class_info.h>
#include <framework/delegate.h>
#include <framework/static_string.hpp>
#include <framework/variant.h>

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace feather {

// The engine's ECS world: flecs underneath, Feather's own vocabulary on top.
//
// What it adds over flecs is registration by reflection. A component type is a
// Component subclass (world/component.h), so it is described in ClassDB, and
// ClassDB is what this listens to -- every Component that registers, whenever it
// registers, becomes a flecs component here without anyone naming its C++ type.
// That is what lets a plugin or a script contribute a component type the engine
// was never compiled against.
class World {
	std::unique_ptr<Ecs::world> _ecs;

	// Component id per registered class name. Also the record of what has
	// already been registered, since flecs would otherwise define it twice.
	std::unordered_map<StaticString, Ecs::entity_t> _components;

	Delegate<std::string_view>::id_t _component_delegate = -1;

	// Modules already imported, by class name. WorldSim both sweeps ClassDB and
	// subscribes to it, so the same module can be reached twice.
	std::unordered_map<StaticString, Ecs::entity_t> _modules;

	// Opens a module scope named after `class_name`, returning the scope to
	// restore. Shared by import_module<T> so the template stays thin.
	Ecs::entity_t _begin_module(StaticString class_name, Entity& out_module);
	void _end_module(Ecs::entity_t previous_scope);

	// Registers whatever Component subclasses ClassDB already knows, then keeps
	// listening. Called from the constructor; see the delegate comment there.
	void _watch_component_registrations();

public:
	World();
	~World();

	World(const World&) = delete;
	World& operator=(const World&) = delete;
	World(World&&) noexcept;
	World& operator=(World&&) noexcept;

	// ---- Simulation --------------------------------------------------------

	bool progress(double delta = 0.0);

	// ---- Entities ----------------------------------------------------------

	[[nodiscard]] Entity create_entity(const std::string& name = "") const;
	[[nodiscard]] Entity create_entity(const Entity& parent, const std::string& name = "") const;
	[[nodiscard]] Entity entity(Ecs::entity_t id) const;
	[[nodiscard]] Entity prefab(const std::string& name) const;
	[[nodiscard]] Entity lookup(const std::string& name) const;
	void destroy_entity(Ecs::entity_t id) const;
	[[nodiscard]] bool is_valid(Ecs::entity_t id) const;

	// ---- Components, by reflection ----------------------------------------

	// Registers `class_name` with the ECS, using the storage its ClassInfo describes. Idempotent: a name already registered returns the
	// id it got the first time. Returns 0 when the class is unknown, is not a value type, or has no storage to give.
	Ecs::entity_t register_component(StaticString class_name);

	// The id a class name was registered under, or 0.
	[[nodiscard]] Ecs::entity_t find_component(StaticString class_name) const;

	// Registers a component whose fields are described at runtime rather than by a C++ type, laying its storage out here. Returns 0 with
	// *error set on a bad description -- a normal outcome, since it comes from outside the engine.
	Ecs::entity_t register_described_component(
			const std::string& name,
			std::vector<ClassInfo::Property> properties,
			ValueTypeOps ops,
			std::string* error = nullptr
	);

	// ---- Components, by C++ type ------------------------------------------

	template <typename T>
	Ecs::entity_t component(const char* name = nullptr);

	template <typename T>
	void set(Ecs::entity_t entity, const T& value) const;

	template <typename T>
	const T* get(Ecs::entity_t entity) const;

	template <typename T>
	bool has(Ecs::entity_t entity) const;

	template <typename T>
	void remove(Ecs::entity_t entity) const;

	template <typename T>
	void remove_all() const;

	// ---- Reflected component access ---------------------------------------

	// Reads or writes one property of a component instance through its
	// reflection accessors, so a caller needs neither the type nor its layout.
	[[nodiscard]] Variant get_property(Ecs::entity_t entity, StaticString class_name, std::string_view property) const;
	bool set_property(Ecs::entity_t entity, StaticString class_name, std::string_view property, const Variant& value);

	bool add_component(Ecs::entity_t entity, StaticString class_name);
	[[nodiscard]] bool has_component(Ecs::entity_t entity, StaticString class_name) const;
	void remove_component(Ecs::entity_t entity, StaticString class_name);

	// Raw storage for a component instance, or nullptr. `mutable_component`
	// also marks it changed, which is what a system writing through it needs.
	[[nodiscard]] const void* component_data(Ecs::entity_t entity, StaticString class_name) const;
	[[nodiscard]] void* mutable_component_data(Ecs::entity_t entity, StaticString class_name);

	// ---- Modules -----------------------------------------------------------

	// Constructs an EcsModule subclass with this world, once, under a module scope named after it so everything it declares is namespaced
	// the way flecs expects. Feather's own import rather than flecs's, because a module is handed this World, not the flecs one.
	template <typename T>
	Entity import_module();

	// The same, for a module reached by class name rather than by type.
	[[nodiscard]] bool is_module_imported(StaticString class_name) const;

	// ---- Escape hatch ------------------------------------------------------

	// flecs itself, for the query and system builders this deliberately does not
	// wrap. Systems are declared through EcsModule; see world/ecs_module.h.
	[[nodiscard]] Ecs::world& ecs() { return *_ecs; }
	[[nodiscard]] const Ecs::world& ecs() const { return *_ecs; }
};

template <typename T>
Ecs::entity_t World::component(const char* name) {
	return name ? _ecs->component<T>(name).id() : _ecs->component<T>().id();
}

template <typename T>
void World::set(Ecs::entity_t entity, const T& value) const {
	_ecs->entity(entity).template set<T>(value);
}

template <typename T>
const T* World::get(Ecs::entity_t entity) const {
	return _ecs->entity(entity).template get<T>();
}

template <typename T>
bool World::has(Ecs::entity_t entity) const {
	return _ecs->entity(entity).template has<T>();
}

template <typename T>
void World::remove(Ecs::entity_t entity) const {
	_ecs->entity(entity).template remove<T>();
}

template <typename T>
void World::remove_all() const {
	_ecs->template remove_all<T>();
}

template <typename T>
Entity World::import_module() {
	const StaticString name = T::get_class_static();
	if (auto it = _modules.find(name); it != _modules.end()) {
		return entity(it->second);
	}

	Entity module_entity;
	const Ecs::entity_t previous = _begin_module(name, module_entity);
	// Recorded before constructing: a module that imports another one during its
	// own constructor must not start this one a second time.
	_modules[name] = module_entity.id();
	{
		T instance(*this);
		(void)instance;
	}
	_end_module(previous);
	return module_entity;
}

} //namespace feather
