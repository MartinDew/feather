#include "register_core_features.h"

#include "register_world_types.gen.h"
#include <math/register_math_types.gen.h>

namespace feather {

void register_core_components(ecs::FeatherWorld world) {
	register_world_components(world);
	register_math_components(world);
}

} //namespace feather