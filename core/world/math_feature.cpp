#include "math_feature.h"

#include "math/transform.h"

namespace feather {

MathWorldFeature::MathWorldFeature() = default;

MathWorldFeature::MathWorldFeature(World& world) {
	world.component<Transform>("Transform");
	world.component<Vector3>("Vector3");
	world.component<Matrix>("Matrix");
	world.component<Color>("Color");
}

} //namespace feather