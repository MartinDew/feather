#include "core_world_feature.h"

#include "components/scene.h"

#include <main/world_sim.h>

namespace feather {

CoreWorldFeature::CoreWorldFeature(World world) {
	world.component<Scene>("Scene");
	world.component<ActiveScene>("ActiveScene");
	auto in_scene = world.component<InScene>();
}

} //namespace feather