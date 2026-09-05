#pragma once
#include "component.h"
#include "ecs_defs.h"
#include "ecs_module.h"
#include "world.h"

// WorldSim must be a complete type here (not just the forward decl from ecs_module.h): the generated register_world_types.gen.cpp
// binds _load_module via ClassDB::bind_static_method, which needs VariantCompatible<WorldSim*> to resolve std::is_base_of_v<Reflected, WorldSim>.
#include <main/world_sim.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "rendering_world_module.gen.h"
#endif

namespace feather {

class Mesh;
class Material;

// Constructors rather than aggregate initialization: a component derives from
// Component, and a base subobject would have to be spelled in a braced list.
struct MeshInstance : Component {
	FSTRUCT();

	std::shared_ptr<Mesh> mesh;

	MeshInstance() = default;
	explicit MeshInstance(std::shared_ptr<Mesh> mesh) : mesh(std::move(mesh)) {}
};

struct MaterialInstance : Component {
	FSTRUCT();

	std::shared_ptr<Material> material; // todo: multiple materials

	MaterialInstance() = default;
	explicit MaterialInstance(std::shared_ptr<Material> material) : material(std::move(material)) {}
};

class RenderingWorldModule : public EcsModule {
	FCLASS(EcsModule);

public:
	RenderingWorldModule() = default;
	RenderingWorldModule(World& world);
};

} //namespace feather