#include "math_module.h"

#include "math/transform.h"

namespace feather {

MathWorldModule::MathWorldModule() = default;

MathWorldModule::MathWorldModule(World& world) {
	// Transform derives from Component, so World registered it from ClassDB already. These are third-party math types that cannot,
	// so they are declared by C++ type here instead.
	world.component<Vector3>("Vector3");
	world.component<Matrix>("Matrix");
	world.component<Color>("Color");
}

} //namespace feather