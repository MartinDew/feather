#pragma once
#include "ecs_defs.h"
#include "ecs_feature.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "math_feature.gen.h"
#endif

namespace feather {

class MathWorldFeature final : public EcsFeature {
	FCLASS();

public:
	MathWorldFeature();
	MathWorldFeature(World& world);
};

} //namespace feather