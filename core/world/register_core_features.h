#pragma once
#include "ecs_defs.h"

namespace feather {

// Registers every reflected Component up front, before any EcsFeature is
// imported, so a component's Flecs name never depends on import order (see
// the ClassDB::get_children_names discovery loop in WorldSim's constructor).
void register_core_components(World& world);

} // namespace feather