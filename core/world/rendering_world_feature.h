#pragma once
#include "ecs_defs.h"
#include "ecs_feature.h"
#include <framework/export_defs.h>

// WorldSim must be a complete type here (not just the forward decl from
// ecs_feature.h): the generated register_world_types.gen.cpp binds
// _load_module via ClassDB::bind_static_method, which needs
// VariantCompatible<WorldSim*> to resolve std::is_base_of_v<Reflected, WorldSim>.
#include <main/world_sim.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "rendering_world_feature.gen.h"
#endif

namespace feather {

class Mesh;
class Material;

struct MeshInstance {
	FSTRUCT(Component);

	std::shared_ptr<Mesh> mesh;
};

struct MaterialInstance {
	FSTRUCT(Component);

	std::shared_ptr<Material> material; // todo: multiple materials
};

class FEATHER_API RenderingWorldFeature : public EcsFeature {
	FCLASS(EcsModule);

public:
	RenderingWorldFeature() = default;
	RenderingWorldFeature(World world);
};

} //namespace feather