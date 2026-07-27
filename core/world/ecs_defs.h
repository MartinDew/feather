#pragma once

#include <flecs.h>
#include <flecs/addons/cpp/entity.hpp>

namespace feather {
using Entity = flecs::entity;
using World = flecs::world;
namespace Ecs = flecs;
using EcsTimer = flecs::timer;
} //namespace feather
