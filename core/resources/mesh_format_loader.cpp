#include "mesh_format_loader.h"

#include "mesh.h"

#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <iostream>

namespace feather {

MeshFormatLoader::MeshFormatLoader() : _importer { std::make_unique<Assimp::Importer>() } {
}

bool MeshFormatLoader::recognize_extension(const std::string& extension) const {
	return _importer->IsExtensionSupported(extension);
}

MeshFormatLoader::~MeshFormatLoader() = default;

std::shared_ptr<Resource> MeshFormatLoader::instantiate(const Path& path) {
	auto mesh = std::make_shared<ComplexMesh>();
	mesh->set_path(path.string());
	return mesh;
}

void MeshFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	const aiScene* scene = _importer->ReadFile(path.string(),
											   aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_FlipUVs |
													   aiProcess_CalcTangentSpace);

	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
		std::cerr << "Assimp error: " << _importer->GetErrorString() << std::endl;
		return;
	}

	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;

	for (unsigned int i = 0; i < scene->mNumMeshes; i++) {
		aiMesh* mesh = scene->mMeshes[i];
		uint32_t index_offset = vertices.size();

		vertices.reserve(mesh->mNumVertices);
		indices.reserve(mesh->mNumFaces * 3);

		for (unsigned int j = 0; j < mesh->mNumVertices; j++) {
			Vertex vertex;
			vertex.position = Vector3(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
			if (mesh->HasNormals()) {
				vertex.normal = Vector3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
			}
			if (mesh->mTextureCoords[0]) {
				vertex.uv = Vector2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y);
			}
			vertices.push_back(vertex);
		}

		for (unsigned int j = 0; j < mesh->mNumFaces; j++) {
			aiFace face = mesh->mFaces[j];
			for (unsigned int k = 0; k < face.mNumIndices; k++) {
				indices.push_back(face.mIndices[k] + index_offset);
			}
		}
	}

	std::static_pointer_cast<ComplexMesh>(resource)->set_mesh_data(vertices, indices);
}

} // namespace feather