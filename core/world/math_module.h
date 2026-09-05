#pragma once
#include "ecs_defs.h"
#include "ecs_module.h"
#include "world.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "math_module.gen.h"
#endif

namespace feather {

class MathWorldModule final : public EcsModule {
	FCLASS(EcsModule);

public:
	MathWorldModule();
	MathWorldModule(World& world);
};

} //namespace feather