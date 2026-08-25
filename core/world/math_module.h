#pragma once
#include "ecs_defs.h"
#include "ecs_module.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "math_module.gen.h"
#endif

namespace feather {

class FEATHER_API MathWorldModule final : public EcsModule {
	FCLASS(EcsModule);

public:
	MathWorldModule();
	MathWorldModule(World& world);
};

} //namespace feather