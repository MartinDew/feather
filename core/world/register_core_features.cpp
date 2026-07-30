#include "register_core_features.h"

#include "core_world_feature.h"
#include "math_feature.h"
#include "rendering_world_feature.h"
#include "register_world_types.gen.h"

namespace feather {

void register_core_ecs_features(World& world) {
	register_world_components(world);

	world.import <MathWorldFeature>();
	world.import <CoreWorldFeature>();
	world.import <RenderingWorldFeature>();
}

} //namespace feather