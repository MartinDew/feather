#pragma once
#include "ecs_defs.h"

namespace feather {

// Registers every reflected Component up front (Scene/ActiveScene/InScene/
// Light/MeshInstance/MaterialInstance/Transform, ...) before any EcsFeature is
// imported, so a component's Flecs name is never at the mercy of which
// feature happens to import first (see the ClassDB::get_children_names
// discovery loop in WorldSim's constructor, which imports every EcsFeature --
// this used to also do that importing directly and in a fixed order, but
// every core feature is FCLASS(EcsModule) now, so the discovery loop alone is
// enough).
void register_core_components(World& world);

} // namespace feather