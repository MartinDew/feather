#include "ecs_module.h"

#include "main/world_sim.h"

namespace feather {

WorldSim* EcsModule::_get_world_sim() {
	return WorldSim::get();
}

} // namespace feather