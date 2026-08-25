#pragma once

#include <framework/export_defs.h>
#include <framework/reflected.h>
#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "ecs_module.gen.h"
#endif

namespace feather {

class WorldSim;

class FEATHER_API EcsModule : public Reflected {
	FCLASS();

protected:
	EcsModule() = default;

	static WorldSim* _get_world_sim();
};

} //namespace feather