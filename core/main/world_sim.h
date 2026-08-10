#pragma once

#include "simulation.h"

#include <framework/reflection_macros.h>
#include <world/ecs_api.h>

#include <memory>

#ifndef FEATHER_REFLECTION_PARSER
#include "world_sim.gen.h"
#endif

namespace feather {

class FEATHER_API WorldSim final : public Simulation {
	FCLASS(singleton);

	// The live flecs world, active-scene bookkeeping, etc. live behind this
	// pimpl (defined in world_sim.cpp) so this header never needs <flecs.h>
	// -- letting a project DLL get a complete WorldSim (required for
	// ClassDB::bind_static_method's VariantCompatible<WorldSim*> check --
	// see e.g. feather-example-project's example_module.h) without flecs
	// ending up on its include path. Engine-internal code that needs richer
	// flecs access than ecs_world()/current_scene_handle() below give it
	// should go through ecs_defs.h's unwrap(), same as
	// rendering_world_feature.cpp does.
	struct Impl;
	std::unique_ptr<Impl> _impl;

	void _create_initial_scene();

public:
	WorldSim();
	~WorldSim() override;

	// Component registration and EcsFeature discovery deliberately live here
	// rather than in the constructor: Engine owns a WorldSim by value, so the
	// constructor runs before Engine::run() calls
	// ResourceLoader::index_project() -- which is what loads the project's
	// extension DLL and lets it register its own EcsFeature/Component types.
	// Running discovery from the constructor made project types permanently
	// invisible; init() is called after index_project(), so they aren't.
	void init() override;

	void update(double delta) override;

	// Plugin-safe handles: opaque, carrying no flecs type, so calling code
	// (project_main.cpp, generated register_<name>_components/systems, an
	// EcsModule's on_import()) never needs <world/ecs_defs.h> or <flecs.h>.
	// Trivial to construct -- see ecs::WorldHandle/EntityHandle's own
	// comments in ecs_api.h.
	[[nodiscard]] ecs::WorldHandle ecs_world() const;
	[[nodiscard]] ecs::EntityHandle current_scene_handle() const;
};

} //namespace feather
