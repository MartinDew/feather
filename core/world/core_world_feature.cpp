#include "core_world_feature.h"

#include "components/scene.h"

#include <main/world_sim.h>

namespace feather {

// Scene/ActiveScene/InScene are registered with Flecs (with their reflected
// names) by register_world_components(), called once up front in
// register_core_ecs_features() -- before any EcsFeature module import, this
// one included -- so there's nothing left to do here.
CoreWorldFeature::CoreWorldFeature(World world) {
}

} //namespace feather