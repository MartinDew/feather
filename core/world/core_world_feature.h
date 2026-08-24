#pragma once
#include "ecs_defs.h"
#include "ecs_module.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "core_world_feature.gen.h"
#endif

namespace feather {

class FEATHER_API CoreWorldFeature : public EcsModule {
	FCLASS(EcsModule);

public:
	CoreWorldFeature() = default;
	CoreWorldFeature(World world);
};

} //namespace feather
