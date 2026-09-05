#include "world_sim.h"

#include "engine.h"
#include "world/ecs_module.h"
#include <world/components/scene.h>
#include <framework/static_string.hpp>

namespace feather {

FSINGLETON_INSTANCE(WorldSim);

WorldSim::WorldSim() : fixed_tick { _world.ecs().timer().interval(Engine::simulation_time) } {
	FSINGLETON_CONSTRUCT_INSTANCE()
#if BETA
	_world.ecs().set<Ecs::Rest>({});
#endif

	// Component types need no registration pass here: World subscribes to ClassDB for Component's children when it is constructed, so
	// everything reflected is already a component, and anything an extension registers later becomes one as it arrives.
}

WorldSim::~WorldSim() {
	ClassDB::unregister_subclass_delegate(EcsModule::get_class_static(), _subclass_delegate_id);
}

void WorldSim::init() {
	// Component types were declared with the world itself (see the constructor), so anything loaded from the project could
	// already query them. What is left is world content and modules.
	_scene_prefab = _world.prefab("Scene");
	auto scene = create_scene("new scene");
	fassert(scene.is_valid());
	set_active_scene(scene);

	// Subscribed here, not in the ctor (which runs before index_project()): registering a class fires this immediately, so a
	// DLL's EcsFeature would otherwise import reentrantly, before the world above exists.
	_subclass_delegate_id = ClassDB::on_subclass_registered(
			EcsModule::get_class_static(), [world_sim = this](std::string_view class_name) {
				if (world_sim) {
					ClassDB::get_static_method(class_name, "_import_module").call(world_sim);
				}
			}
	);

	// Picks up EcsModule subclasses from core, from built-in modules, and --
	// because this runs after index_project() -- from loaded project DLLs.
	auto children = ClassDB::get_children_names(EcsModule::get_class_static());
	for (auto& child : children) {
		ClassDB::get_static_method(child, "_import_module").call(this);
	}
}

void WorldSim::update(double delta) {
	// Ecs::query<Transform, MeshInstance, MaterialInstance> q

	bool result = _world.progress(delta);
}

Entity WorldSim::create_scene(const std::string& name) const {
	Scene s { { name } };
	return _world.create_entity(name).is_a(_scene_prefab).set<Scene>(s);
}

Entity WorldSim::create_entity(const std::string& name) const {
	return _world.create_entity(name);
}

Entity WorldSim::create_entity(const Entity& parent_entity, const std::string& name) const {
	return _world.create_entity(parent_entity, name);
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