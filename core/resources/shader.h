#pragma once
#include "resource.h"

#include <filesystem>

#ifndef FEATHER_REFLECTION_PARSER
#include "shader.gen.h"
#endif

namespace feather {

class RenderingServer;

class Shader : public Resource {
	FCLASS();
	friend RenderingServer;

	enum class ShaderSourceType : uint8_t {
		INVALID = 0,
		PATH,
		SOURCE,
	};

	ShaderSourceType _shader_source_type = ShaderSourceType::INVALID;
	std::string _shader_source = "";
	std::string _vertex_entry = "VSMain";
	std::string _pixel_entry = "PSMain";

public:
	Shader() = default;

	void set_shader_path(std::string_view path);
	void set_shader_code(std::string_view code);
	void set_vertex_entry(std::string_view entry) { _vertex_entry = entry; }
	void set_pixel_entry(std::string_view entry) { _pixel_entry = entry; }

	bool is_valid() const;
	bool is_path_based() const { return _shader_source_type == ShaderSourceType::PATH; }
	bool is_source_based() const { return _shader_source_type == ShaderSourceType::SOURCE; }
	std::string_view get_source() const { return _shader_source; }
	std::string_view get_vertex_entry() const { return _vertex_entry; }
	std::string_view get_pixel_entry() const { return _pixel_entry; }
};

} //namespace feather