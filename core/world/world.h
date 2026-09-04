// world.h
#pragma once

#include "ecs_defs.h"

#include <framework/variant.h>
#include <framework/static_string.hpp>

#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace feather {

// Implementation for an ECS world
class World : public Reflected {
	FCLASS(singleton);

	std::unique_ptr<flecs::world> _ecs_world;
	std::unordered_map<StaticString, ecs_entity_t> _component_type_map;

public:
	World();
	~World();

	World(const World&) = delete;
	World& operator=(const World&) = delete;

	World(World&&) noexcept;
	World& operator=(World&&) noexcept;

	// Progress the Flecs simulation tick
	bool progress(float delta);

	// Entity operations
	ecs_entity_t entity_create();
	void entity_destroy(ecs_entity_t entity);
	bool entity_is_valid(ecs_entity_t entity) const;

	// Registration from reflection / ClassDB
	ecs_entity_t register_component_type(StaticString class_name);

	// Component manipulation via Variant / raw void pointers
	void set_component_value(ecs_entity_t entity, StaticString class_name, const Variant& value);
	Variant get_component_value(ecs_entity_t entity, StaticString class_name) const;

	bool has_component(ecs_entity_t entity, StaticString class_name) const;
	void remove_component(ecs_entity_t entity, StaticString class_name);

	// Dynamic System creation via ClassDB reflected function / symbol
	ecs_entity_t
	create_system(std::string_view system_name, std::span<const StaticString> components, StaticString method_name);

	// Typed C++ API for convenience
	template <typename T>
	ecs_entity_t component();

	template <typename T>
	void set(ecs_entity_t entity, const T& component);

	template <typename T>
	const T* get(ecs_entity_t entity) const;

	template <typename T>
	bool has(ecs_entity_t entity) const;

	template <typename T>
	void remove(ecs_entity_t entity);

	// Native Flecs handles
	flecs::world& get_flecs_world();
	const flecs::world& get_flecs_world() const;
};

template <typename T>
ecs_entity_t World::component() {
	return _ecs_world->component<T>().id();
}

template <typename T>
void World::set(ecs_entity_t entity, const T& comp) {
	_ecs_world->entity(entity).set<T>(comp);
}

template <typename T>
const T* World::get(ecs_entity_t entity) const {
	return _ecs_world->entity(entity).get<T>();
}

template <typename T>
bool World::has(ecs_entity_t entity) const {
	return _ecs_world->entity(entity).has<T>();
}

template <typename T>
void World::remove(ecs_entity_t entity) {
	_ecs_world->entity(entity).remove<T>();
}

} // namespace feather