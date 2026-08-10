#pragma once

#include "ecs_api.h"

#include <flecs.h>
#include <flecs/addons/cpp/entity.hpp>

namespace feather {
using Entity = flecs::entity;
using World = flecs::world;
namespace Ecs = flecs;
using EcsTimer = flecs::timer;

// Engine-internal escape hatch: ecs_api.h's WorldHandle/EntityHandle carry
// no flecs type so a plugin never needs <flecs.h>, but engine code that
// already links flecs (this header) can freely unwrap them back into
// flecs::world/flecs::entity to reach flecs's richer C++ builder API --
// exactly what an EcsModule::on_import(WorldHandle, EntityHandle)
// implementation does for the same reason rendering_world_feature.cpp's
// system builders need it. Non-owning: flecs::world's ecs_world_t*
// constructor bumps a refcount (flecs_poly_claim) rather than taking
// ownership, so this never outlives or double-frees the real world WorldSim
// owns.
inline World unwrap(ecs::WorldHandle w) {
	return World(static_cast<ecs_world_t*>(w._handle));
}
inline Entity unwrap(ecs::EntityHandle e) {
	return Entity(static_cast<const ecs_world_t*>(e.world), static_cast<ecs_entity_t>(e.id));
}
} //namespace feather
