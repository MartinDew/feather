#include "mesh_data.h"

namespace feather {

MeshData::MeshData(std::vector<Vertex> vertices, std::vector<Index> indices)
		: _vertices { vertices.begin(), vertices.end() }
		, _indices { indices.begin(), indices.end() } {
}

const CowVector<Vertex>& MeshData::get_vertices() const {
	return _vertices;
}

const CowVector<Index>& MeshData::get_indices() const {
	return _indices;
}

void MeshData::set_vertices(const CowVector<Vertex>& vertices) {
	_vertices = vertices;
}

void MeshData::set_indices(const CowVector<Index>& indices) {
	_indices = indices;
}

} //namespace feather