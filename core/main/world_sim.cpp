#include "world_sim.h"

#include "engine.h"
#include "world/ecs_feature.h"
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

WorldSim::~WorldSim() {
	ClassDB::unregister_subclass_delegate(EcsFeature::get_class_static(), _subclass_delegate_id);
}

void WorldSim::init() {
	// Every reflected Component registers before any EcsFeature imports
	register_core_components(_world);

	_scene_prefab = _world.prefab("Scene");
	auto scene = create_scene("new scene");
	fassert(scene.is_valid());
	set_active_scene(scene);

	// Subscribed here, not in the constructor: registering a class fires this
	// delegate immediately (see _fire_subclass_delegates), and the ctor runs
	// well before index_project() -- a project DLL's own EcsFeature subclass
	// would otherwise get _import_module called on it reentrantly, from
	// inside index_project()'s own directory scan, before the world above is
	// even set up. Subscribing here instead means nothing can fire until
	// this point is reached.
	_subclass_delegate_id = ClassDB::on_subclass_registered(
			EcsFeature::get_class_static(), [world_sim = this](std::string_view class_name) {
				if (world_sim) {
					ClassDB::get_static_method(class_name, "_import_module").call(world_sim);
				}
			});

	// Picks up EcsFeature subclasses from core, from built-in modules, and --
	// because this runs after index_project() -- from loaded project DLLs.
	auto children = ClassDB::get_children_names(EcsFeature::get_class_static());
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