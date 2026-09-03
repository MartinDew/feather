#pragma once
#include "ecs_defs.h"
#include "ecs_module.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "core_world_module.gen.h"
#endif

namespace feather {

class CoreWorldModule : public EcsModule {
	FCLASS(EcsModule);

public:
	CoreWorldModule() = default;
	CoreWorldModule(World world);
};

} //namespace feather
