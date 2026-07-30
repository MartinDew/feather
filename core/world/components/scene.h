#pragma once
#include <world/ecs_defs.h>
#include <framework/reflection_macros.h>
#include <framework/static_string.hpp>

#include <cstdint>

#ifndef FEATHER_REFLECTION_PARSER
#include "scene.gen.h"
#endif

namespace feather {

struct Scene {
	FSTRUCT(Component);

	[[get, set]] int64_t _scene_id = -1;

	Scene() = default;
	Scene(StaticString name);
	Scene(const Scene&) = default;
	Scene& operator=(const Scene&) = default;
	Scene(Scene&&) = default;
	Scene& operator=(Scene&&) = default;

	bool operator==(const Scene& other) const;
};

// Relationship Tag
struct ActiveScene {
	FSTRUCT(Component);
};

struct InScene {
	FSTRUCT(Component);
};

} //namespace feather