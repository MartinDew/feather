#pragma once

#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "component.gen.h"
#endif

namespace feather {

// The base every ECS component derives from. Empty on purpose: it carries no
// storage and adds no size (an empty base is folded away), so a component stays
// exactly the struct its fields describe and remains standard-layout.
//
// What deriving from it buys is registration. ClassDB records the parent of
// every reflected type, so World can ask for Component's children and be told
// about new ones as they arrive -- which is how a component type registers with
// the ECS without anyone naming its C++ type (world/world.cpp). Nothing else
// needs doing: no modifier, no per-directory registration function.
struct Component {
	FSTRUCT();
};

} //namespace feather
