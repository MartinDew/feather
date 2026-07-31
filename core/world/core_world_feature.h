#pragma once
#include "ecs_defs.h"
#include "ecs_feature.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "core_world_feature.gen.h"
#endif

namespace feather {

class CoreWorldFeature : public EcsFeature {
	FCLASS(EcsModule);

public:
	CoreWorldFeature() = default;
	CoreWorldFeature(World world);
};

} //namespace feather
