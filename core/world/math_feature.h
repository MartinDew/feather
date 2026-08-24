#pragma once
#include "ecs_defs.h"
#include "ecs_module.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "math_feature.gen.h"
#endif

namespace feather {

class FEATHER_API MathWorldFeature final : public EcsModule {
	FCLASS(EcsModule);

public:
	MathWorldFeature();
	MathWorldFeature(World& world);
};

} //namespace feather