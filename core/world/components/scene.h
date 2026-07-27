#pragma once
#include <world/ecs_defs.h>
#include <framework/static_string.hpp>

#include <cstdint>

namespace feather {

struct Scene {
	int64_t _scene_id = -1;

	Scene() = default;
	Scene(StaticString name);
	Scene(const Scene&) = default;
	Scene& operator=(const Scene&) = default;
	Scene(Scene&&) = default;
	Scene& operator=(Scene&&) = default;

	bool operator==(const Scene& other) const;
};

// Relationship Tag
struct ActiveScene {};

struct InScene {};

} //namespace feather