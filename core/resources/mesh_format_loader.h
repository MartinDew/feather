#pragma once

#include "resource_format_loader.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "mesh_format_loader.gen.h"
#endif

namespace Assimp {
class Importer;
};

namespace feather {

// Todo : mesh format loader needs to be reworked because we don't necessarily load only mesh.
// It's possible to load a full scene. The problem is identifying the underlying meshes afterwards from the VFP.
class MeshFormatLoader : public ResourceFormatLoader {
	FCLASS();

	std::unique_ptr<Assimp::Importer> _importer;

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;

public:
	MeshFormatLoader();
	~MeshFormatLoader() override;

	[[method]]
	bool recognize_extension(const std::string& extension) const override;
};

} //namespace feather
