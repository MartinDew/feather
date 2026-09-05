#pragma once

#include <framework/reflection_macros.h>
#include <math/math_defs.h>
#include <world/component.h>
#include <cstdint>

#ifndef FEATHER_REFLECTION_PARSER
#include "light.gen.h"
#endif

namespace feather {

// Free enum, not nested in Light: FSTRUCT's generated accessors are emitted at
// the top of the class body, before a nested type declared later would be
// visible to name lookup -- same reason Vector3/Color aren't nested either.
enum class LightType : uint8_t {
	Directional,
	Point,
	Spot
};

struct Light : Component {
	FSTRUCT();

	[[get, set]] LightType type = LightType::Directional;
	[[get, set]] Vector3 position = Vector3::zero;
	[[get, set]] Vector3 direction = Vector3::forward;
	[[get, set]] Color color = Color(1.0f, 1.0f, 1.0f, 1.0f);
	[[get, set]] float intensity = 1.0f;
	[[get, set]] float range = 100.0f; // For point/spot lights
	[[get, set]] float spot_angle = 45.0f; // For spot lights (in degrees)
	[[get, set]] bool cast_shadows = true;
};

} //namespace feather