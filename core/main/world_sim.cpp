#include "world_sim.h"

#include "engine.h"
#include <world/components/scene.h>
#include <world/ecs_defs.h>
#include <world/register_core_features.h>
#include <framework/static_string.hpp>

#include <flecs/addons/cpp/world.hpp>

namespace feather {

struct WorldSim::Impl {
	World world;
	Entity scene_prefab;
	Entity current_scene;
};

FSINGLETON_INSTANCE(WorldSim);

WorldSim::WorldSim() : _impl(std::make_unique<Impl>()) {
	FSINGLETON_CONSTRUCT_INSTANCE()
#if BETA
	_impl->world.set<Ecs::Rest>({});
#endif
}

WorldSim::~WorldSim() = default;

void WorldSim::init() {
	register_core_components(ecs_world());

	_create_initial_scene();

	// Picks up EcsFeature subclasses from core, from built-in modules, and --
	// because this runs after index_project() -- from loaded project DLLs.
	auto children = ClassDB::get_children_names("EcsFeature");
	for (auto& child : children) {
		ClassDB::get_static_method(child, "_import_module").call(this);
	}
}

void WorldSim::update(double delta) {
	bool result = _impl->world.progress(/*delta*/);
}

void WorldSim::_create_initial_scene() {
	_impl->scene_prefab = _impl->world.prefab("Scene");

	Scene s { { "new scene" } };
	Entity scene = _impl->world.entity("new scene").is_a(_impl->scene_prefab).set<Scene>(s);
	fassert(scene.is_valid());
	fassert(scene.is_a(_impl->scene_prefab), "Given scene isn't a scene instance");

	// Clear old active scene marker (there is none yet, but this mirrors the
	// invariant a future set_active_scene() would need to preserve: exactly
	// one ActiveScene at a time).
	_impl->world.remove_all<ActiveScene>();
	scene.add<ActiveScene>();
	_impl->current_scene = scene;
}

ecs::FeatherWorld WorldSim::ecs_world() const {
	return ecs::FeatherWorld { _impl->world.c_ptr() };
}

ecs::FeatherEntity WorldSim::current_scene_handle() const {
	return ecs::FeatherEntity { _impl->current_scene.raw_id(), _impl->world.c_ptr() };
}

} //namespace feather
