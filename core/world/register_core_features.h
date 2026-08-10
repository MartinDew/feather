#pragma once
#include "ecs_api.h"

namespace feather {

// Registers every reflected Component up front, before any EcsFeature is
// imported, so a component's Flecs name never depends on import order (see
// the ClassDB::get_children_names discovery loop in WorldSim's constructor).
void register_core_components(ecs::FeatherWorld world);

} // namespace feather