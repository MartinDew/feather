#include "world_sim.h"

#include "engine.h"
#include <world/components/scene.h>
#include <world/register_core_features.h>
#include <framework/static_string.hpp>

namespace feather {

FSINGLETON_INSTANCE(WorldSim);

WorldSim::WorldSim() : fixed_tick { _world.timer().interval(Engine::simulation_time) } {
	FSINGLETON_CONSTRUCT_INSTANCE()
#if BETA
	_world.set<Ecs::Rest>({});
#endif
}

WorldSim::~WorldSim() = default;

// Todo : The world_sim should probably use classDB registration delegates rather than late loading EcsModules
void WorldSim::init() {
	register_core_components(_world);

	_scene_prefab = _world.prefab("Scene");
	auto scene = create_scene("new scene");
	fassert(scene.is_valid());
	set_active_scene(scene);

	// Picks up EcsFeature subclasses from core, from built-in modules, and initially loaded dlls.
	auto children = ClassDB::get_children_names("EcsFeature");
	for (auto& child : children) {
		ClassDB::get_static_method(child, "_import_module").call(this);
	}
}

void WorldSim::update(double delta) {
	// Ecs::query<Transform, MeshInstance, MaterialInstance> q

	bool result = _world.progress(/*delta*/);
}

Entity WorldSim::create_scene(std::string name) const {
	Scene s { { name } };
	return _world.entity(name.c_str()).is_a(_scene_prefab).set<Scene>(s);
}

Entity WorldSim::create_entity(std::string name) const {
	return _world.entity(name.c_str());
}

Entity WorldSim::create_entity(const Entity& parent_entity, std::string name) const {
	return _world.entity(name.c_str()).child_of(parent_entity);
}

void WorldSim::add_to_scene(Entity entity) const {
	entity.child_of(_current_scene);
}

bool WorldSim::_is_in_scene(flecs::entity e, Entity scene) const {
	flecs::entity current = e;
	while (current.is_valid()) {
		if (current == scene)
			return true;
		current = current.parent();
	}
	return false;
}

void WorldSim::set_active_scene(Entity scene) {
	fassert(scene.is_a(_scene_prefab), "Given scene isn't a scene instance");

	// Clear old active scene marker
	_world.remove_all<ActiveScene>();

	scene.add<ActiveScene>();
	_current_scene = scene;
}

} //namespace feather