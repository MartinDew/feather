#pragma once

#include "framework/cow_vector.h"
#include "framework/export_defs.h"
#include <math/math_defs.h>
#include <array>
#include <vector>

namespace feather {

using Index = uint32_t;

class FEATHER_API MeshData {
public:
	MeshData() = default;
	MeshData(std::vector<Vertex> vertices, std::vector<Index> indices);

	const CowVector<Vertex>& get_vertices() const;
	const CowVector<Index>& get_indices() const;

	void set_vertices(const CowVector<Vertex>& vertices);
	void set_indices(const CowVector<Index>& indices);

protected:
	CowVector<Vertex> _vertices {};
	CowVector<Index> _indices {};
};

} //namespace feather