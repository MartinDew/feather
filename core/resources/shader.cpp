#include "shader.h"

#include <framework/reflection_macros.h>

namespace feather {

void Shader::set_shader_path(std::string_view path) {
	_shader_source_type = ShaderSourceType::PATH;
	_shader_source = path;
}

void Shader::set_shader_code(std::string_view code) {
	_shader_source_type = ShaderSourceType::SOURCE;
	_shader_source = code;
}

bool Shader::is_valid() const {
	return _shader_source_type != ShaderSourceType::INVALID;
}

} //namespace feather
