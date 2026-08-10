#pragma once
#include "ecs_api.h"
#include "ecs_feature.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "core_world_feature.gen.h"
#endif

namespace feather {

class CoreWorldFeature : public EcsFeature {
	FCLASS(EcsModule);

public:
	CoreWorldFeature() = default;

	static void on_import(ecs::FeatherWorld world, ecs::FeatherEntity scene);
};

} //namespace feather
