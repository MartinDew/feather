#pragma once

#include "world.h"

#include <framework/reflected.h>
#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "ecs_module.gen.h"
#endif

namespace feather {

class WorldSim;

// A feature's worth of ECS content: the components it needs declared by type,
// and the systems it runs. WorldSim finds every subclass through ClassDB and
// imports it (world/world.h's import_module), which constructs it with the
// world -- so a module's constructor is where it declares what it owns.
class EcsModule : public Reflected {
	FCLASS();

protected:
	EcsModule() = default;

	static WorldSim* _get_world_sim();

	// The way a system is declared. Protected on purpose: a system belongs to the module that declares it, so a stray function
	// cannot register one. The [[system]] attribute holds a method to the same rule.
	template <class... TComps>
	static Ecs::system_builder<TComps...> system(World& world, const char* name) {
		return world.ecs().system<TComps...>(name);
	}
};

} //namespace feather
