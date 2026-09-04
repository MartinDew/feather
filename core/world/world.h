#pragma once

namespace flecs {
class world;
}

namespace feather {

// Ecs world
class World : public Reflected {
	FCLASS(World);

	flecs::world* _world;
};

} //namespace feather