#include "core_world_module.h"

#include "components/scene.h"

#include <main/world_sim.h>

namespace feather {

// Scene/ActiveScene/InScene reach the ECS through World's ClassDB subscription
// the moment they are reflected, so there is nothing left to do here.
CoreWorldModule::CoreWorldModule(World& world) {
}

} //namespace feather