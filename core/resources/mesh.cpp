#include "mesh.h"

#include "framework/container_utils.h"
#include "framework/variant_array.h"
#include "math/math_defs.h"
#include "rendering/mesh_data.h"
#include <core/framework/variant.h>
#include <core/main/class_db.h>
#include <framework/reflection_macros.h>
#include <set>
#include <vector>

namespace feather {

Mesh::Mesh(const std::shared_ptr<MeshData>& mesh_data) : _mesh_data(mesh_data) {
}

void Mesh::set_vertices(const CowVector<Vertex>& vertices) {
	if (_mesh_data) {
		_mesh_data->set_vertices(vertices);
	}
}

void Mesh::set_indices(const CowVector<Index>& indices) {
	if (_mesh_data) {
		_mesh_data->set_indices(indices);
	}
}

// object_create("ComplexMesh") (the plugin ABI's factory path -- see
// classdb_register_extension_class in feather_interface.cpp) default-
// constructs one with no format loader involved, unlike a normal Mesh
// load; add_vertices/add_indices write to _mesh_data unconditionally, so
// it must never be null.
ComplexMesh::ComplexMesh() : Mesh(std::make_shared<MeshData>()) {
}

void ComplexMesh::add_vertices(const VariantArray vertices) {
	CowVector<Vertex> raw_vertices;
	if (_mesh_data) {
		raw_vertices = _mesh_data->get_vertices();
	}
	raw_vertices.reserve(raw_vertices.size() + vertices.size());
	for (const auto& v : vertices) {
		if (v.is_type(VariantType::VERTEX)) {
			raw_vertices.push_back(v.as<Vertex>().value());
		}
	}

	_mesh_data->set_vertices(raw_vertices);
}

void ComplexMesh::add_indices(const VariantArray indices) {
	CowVector<uint32_t> raw_indices;
	if (_mesh_data) {
		raw_indices = _mesh_data->get_indices();
	}
	raw_indices.reserve(raw_indices.size() + indices.size());
	for (const auto& v : indices) {
		if (v.is_type(VariantType::INT)) {
			raw_indices.push_back(v.as<int>().value());
		}
	}

	_mesh_data->set_indices(raw_indices);
}

void ComplexMesh::set_mesh_data(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
	_mesh_data = std::make_shared<MeshData>(vertices, indices);
}

VariantArray ComplexMesh::get_vertices() const {
	if (!_mesh_data)
		return VariantArray();
	return { _mesh_data->get_vertices().begin(), _mesh_data->get_vertices().end() };
}

VariantArray ComplexMesh::get_indices() const {
	if (!_mesh_data)
		return VariantArray();
	return { _mesh_data->get_indices().begin(), _mesh_data->get_indices().end() };
}

//// Box Mesh ////

static constexpr float DX = 1;
static constexpr float DY = 1;
static constexpr float DZ = 1;

static constexpr Vector3 Point[8] = { Vector3(-DX / 2, DY / 2, -DZ / 2), Vector3(DX / 2, DY / 2, -DZ / 2),
									  Vector3(DX / 2, -DY / 2, -DZ / 2), Vector3(-DX / 2, -DY / 2, -DZ / 2),
									  Vector3(-DX / 2, DY / 2, DZ / 2),	 Vector3(-DX / 2, -DY / 2, DZ / 2),
									  Vector3(DX / 2, -DY / 2, DZ / 2),	 Vector3(DX / 2, DY / 2, DZ / 2) };

// Normals
static constexpr Vector3 N0(0.0f, 0.0f, -1.0f); // front
static constexpr Vector3 N1(0.0f, 0.0f, 1.0f); // back
static constexpr Vector3 N2(0.0f, -1.0f, 0.0f); // down
static constexpr Vector3 N3(0.0f, 1.0f, 0.0f); // up
static constexpr Vector3 N4(-1.0f, 0.0f, 0.0f); // left
static constexpr Vector3 N5(1.0f, 0.0f, 0.0f); // right

// clang-format off

static constexpr std::array<Vertex, 24> cube_vertices {
	Vertex { Point[0], N0, Vector2(0.0f, 1.0f)},
	Vertex { Point[1], N0, Vector2(1.0f, 1.0f)},
	Vertex { Point[2], N0, Vector2(1.0f, 0.0f)},
	Vertex { Point[3], N0, Vector2(0.0f, 0.0f)},
	Vertex { Point[4], N1, Vector2(1.0f, 1.0f)},
	Vertex { Point[5], N1, Vector2(1.0f, 0.0f)},
	Vertex { Point[6], N1, Vector2(0.0f, 0.0f)},
	Vertex { Point[7], N1, Vector2(0.0f, 1.0f)},
	Vertex { Point[3], N2, Vector2(0.0f, 1.0f)},
	Vertex { Point[2], N2, Vector2(1.0f, 1.0f)},
	Vertex { Point[6], N2, Vector2(1.0f, 0.0f)},
	Vertex { Point[5], N2, Vector2(0.0f, 0.0f)},
	Vertex { Point[0], N3, Vector2(0.0f, 0.0f)},
	Vertex { Point[4], N3, Vector2(0.0f, 1.0f)},
	Vertex { Point[7], N3, Vector2(1.0f, 1.0f)},
	Vertex { Point[1], N3, Vector2(1.0f, 0.0f)},
	Vertex { Point[0], N4, Vector2(1.0f, 1.0f)},
	Vertex { Point[3], N4, Vector2(1.0f, 0.0f)},
	Vertex { Point[5], N4, Vector2(0.0f, 0.0f)},
	Vertex { Point[4], N4, Vector2(0.0f, 1.0f)},
	Vertex { Point[1], N5, Vector2(0.0f, 1.0f)},
	Vertex { Point[7], N5, Vector2(1.0f, 1.0f)},
	Vertex { Point[6], N5, Vector2(1.0f, 0.0f)},
	Vertex { Point[2], N5, Vector2(0.0f, 0.0f) }
};

// clang-format on

constexpr std::array<uint32_t, 36> cube_indices {
	// Front face
	0,	1,	2, // front
	0,	2,	3, // front

	5,	6,	7, // back
	5,	7,	4, // back

	8,	9,	10, // down
	8,	10, 11, // down

	13, 14, 15, // up
	13, 15, 12, // up

	19, 16, 17, // left
	19, 17, 18, // left
	20, 21, 22, // right
	20, 22, 23 // right
};

static std::shared_ptr<MeshData> box_mesh =
		std::make_shared<MeshData>(std::vector<Vertex> { cube_vertices.begin(), cube_vertices.end() },
								   std::vector<uint32_t> { cube_indices.begin(), cube_indices.end() });

BoxMesh::BoxMesh() : Super(box_mesh) {
}

} // namespace feather