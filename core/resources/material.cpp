#include "material.h"

#include "shader.h"
#include "texture.h"
#include <framework/variant.h>
#include <main/class_db.h>

namespace feather {

static std::shared_ptr<Shader> placeholder_mat_shader;

PlaceholderMaterial::PlaceholderMaterial() {
	if (!placeholder_mat_shader) {
		placeholder_mat_shader = std::make_shared<Shader>();
		// placeholder_mat_shader->set_shader_code(placeholder_mat);
	}

	_shader = placeholder_mat_shader;
}

} //namespace feather