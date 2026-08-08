#pragma once

#include "simulation.h"

#include <framework/reflection_macros.h>
#include <world/components/scene.h>
#include <world/ecs_defs.h>

#include <flecs.h>
#include <flecs/addons/cpp/world.hpp>

#ifndef FEATHER_REFLECTION_PARSER
#include "world_sim.gen.h"
#endif

namespace feather {

class WorldSim final : public Simulation {
	FCLASS(singleton);

	World _world;
	Entity _scene_prefab;
	Entity _current_scene;

	std::vector<Entity> _scenes;

	// In world_sim.h, private section:
	bool _is_in_scene(flecs::entity e, Entity scene) const;

	template <class... TComps, class TFunc>
	void _iterate_tree(flecs::entity e, TFunc func) {
		// Todo
	}

protected:
	template <std::derived_from<class EcsFeature> T>
	void _import_feature() {
		_world.import<T>();
	}

public:
	const EcsTimer fixed_tick;

	WorldSim();
	~WorldSim() override;

	// Component registration and EcsFeature discovery deliberately live here
	// rather than in the constructor: Engine owns a WorldSim by value, so the
	// constructor runs before Engine::run() calls
	// ResourceLoader::index_project() -- which is what loads the project's
	// extension DLL and lets it register its own EcsFeature/Component types.
	// Running discovery from the constructor made project types permanently
	// invisible; init() is called after index_project(), so they aren't.
	void init() override;

	void update(double delta) override;

	[[nodiscard]] Entity& get_current_scene() { return _current_scene; }

	[[nodiscard]]
	Entity create_scene(std::string name) const;

	[[nodiscard]]
	Entity create_entity(std::string name = "") const;
	[[nodiscard]]
	Entity create_entity(const Entity& parent_entity, std::string name = "") const;

	// get low level world impl
	[[nodiscard]] World* get_world() { return &_world; }

	void add_to_scene(Entity entity) const;

	template <class... T>
	Ecs::system_builder<T...>& execute_fixed(Ecs::system_builder<T...>& system) {
		return system.tick_source(fixed_tick);
	}

	void set_active_scene(Entity scene);

	// Build a query scoped to the active scene
	template <class... TComps>
	auto scene_query() {
		return _world.query_builder<TComps...>().template with<ActiveScene>().up(Ecs::ChildOf).build();
	}

	template <class... TComps>
	auto scene_system() {

	};
};

} //namespace feather