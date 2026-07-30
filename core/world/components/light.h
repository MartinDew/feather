#pragma once

#include <framework/reflection_macros.h>
#include <math/math_defs.h>
#include <cstdint>

#ifndef FEATHER_REFLECTION_PARSER
#include "light.gen.h"
#endif

namespace feather {

// Free enum, not nested in Light: FSTRUCT()/FCLASS() must be the first thing
// in the class body, so a generated inline accessor's return type (e.g.
// `LightType get_type() const`) is textually emitted at that same point --
// before any nested type declared later in the class would be visible to
// ordinary (non-complete-class-context) name lookup. A property's type has to
// already be visible at the top of the class, same as Vector3/Color below.
enum class LightType : uint8_t {
	Directional,
	Point,
	Spot
};

struct Light {
	FSTRUCT(Component);

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