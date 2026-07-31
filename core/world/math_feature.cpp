#include "math_feature.h"

#include "math/transform.h"

namespace feather {

MathWorldFeature::MathWorldFeature() = default;

MathWorldFeature::MathWorldFeature(World& world) {
	// Transform is registered up front (with everyone else's Component types)
	// by register_math_components(), called before any feature import -- see
	// register_core_features.cpp. Vector3/Matrix/Color are DirectX::SimpleMath
	// aliases, not FeatherEngine types, so they can't carry FSTRUCT(Component)
	// and stay registered here instead.
	world.component<Vector3>("Vector3");
	world.component<Matrix>("Matrix");
	world.component<Color>("Color");
}

} //namespace feather