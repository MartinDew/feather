#pragma once

#include "framework/variant_array.h"
#include "math/math_defs.h"
#include "resource.h"
#include <core/framework/reflection_macros.h>
#include <core/rendering/mesh_data.h>

#include <memory>
#include <vector>

#ifndef FEATHER_REFLECTION_PARSER
#include "mesh.gen.h"
#endif

namespace feather {

class Mesh : public Resource {
	FCLASS();

protected:
	std::shared_ptr<MeshData> _mesh_data;

	Mesh() = default;
	explicit Mesh(const std::shared_ptr<MeshData>& mesh_data);

protected:
	void set_vertices(const CowVector<Vertex>& vertices);
	void set_indices(const CowVector<Index>& indices);

public:
	const std::shared_ptr<MeshData>& get_mesh_data() { return _mesh_data; };
};

// Mesh using raw vertices and indices data
class ComplexMesh : public Mesh {
	FCLASS();

public:
	ComplexMesh();

	[[method]]
	void add_vertices(const VariantArray vertices);
	[[method]]
	void add_indices(const VariantArray indices);

	[[method]]
	VariantArray get_vertices() const;
	[[method]]
	VariantArray get_indices() const;

	void set_mesh_data(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
};

class BoxMesh : public Mesh {
	FCLASS();

public:
	BoxMesh();
};

} // namespace feather