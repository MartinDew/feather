#include "math_feature.h"

#include "ecs_api.h"
#include "math/transform.h"

namespace feather {

void MathWorldFeature::on_import(ecs::FeatherWorld world, ecs::FeatherEntity scene) {
	// Transform is registered up front (with everyone else's Component types)
	// by register_math_components(), called before any feature import -- see
	// register_core_features.cpp. Vector3/Matrix/Color are DirectX::SimpleMath
	// aliases, not FeatherEngine types, so they can't carry FSTRUCT(Component)
	// and stay registered here instead -- via ecs::register_component(), not
	// flecs's own world.component<T>() template, for the same reason
	// ComponentModifier's generated code does: keeps this call site exercising
	// the exact API a plugin would use, even though this .cpp has full flecs
	// access (via ecs_defs.h's unwrap()) and could call the template directly.
	ecs::register_component(world, ecs::make_component_desc<Vector3>("Vector3"));
	ecs::register_component(world, ecs::make_component_desc<Matrix>("Matrix"));
	ecs::register_component(world, ecs::make_component_desc<Color>("Color"));
}

} //namespace feather