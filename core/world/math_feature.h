#pragma once
#include "ecs_api.h"
#include "ecs_feature.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "math_feature.gen.h"
#endif

namespace feather {

class MathWorldFeature final : public EcsFeature {
	FCLASS(EcsModule);

public:
	MathWorldFeature() = default;

	static void on_import(ecs::WorldHandle world, ecs::EntityHandle scene);
};

} //namespace feather