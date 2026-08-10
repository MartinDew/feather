#pragma once

#include "resource.h"
#include <core/math/math_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "material.gen.h"
#endif

namespace feather {

class RenderingServer;
class Shader;
class Texture;

class FEATHER_API Material : public Resource {
	FCLASS();
	friend RenderingServer;

protected:
	std::shared_ptr<Shader> _shader;

	// Todo : shader params

public:
	std::shared_ptr<Shader> get_shader() const { return _shader; }
};

class FEATHER_API PlaceholderMaterial : public Material {
	FCLASS();

public:
	PlaceholderMaterial();
};

class FEATHER_API ShaderMaterial : public Material {
	FCLASS();

public:
	void set_shader(std::shared_ptr<Shader> shader) { _shader = std::move(shader); }
};

class FEATHER_API PBRMaterial : public Material {
	FCLASS();

protected:
	// Textures
	std::shared_ptr<Texture> _base_color_texture;
	std::shared_ptr<Texture> _metallic_roughness_texture;
	std::shared_ptr<Texture> _normal_texture;
	std::shared_ptr<Texture> _emissive_texture;

	// Material factors (multiply with texture samples)
	[[get(public), set(public)]]
	Color _base_color_factor = Color(1.0f, 1.0f, 1.0f, 1.0f);
	[[get(public), set(public)]]
	float _metallic_factor = 1.0f;
	[[get(public), set(public)]]
	float _roughness_factor = 1.0f;
	[[get(public), set(public)]]
	Color _emissive_factor = Color(0.0f, 0.0f, 0.0f, 1.0f);

	// Rendering options
	[[get(public), set(public)]]
	bool _alpha_blend = false;
	[[get(public), set(public)]]
	bool _double_sided = false;

public:
	PBRMaterial() = default;

	// Texture getters/setters
	void set_base_color_texture(std::shared_ptr<Texture> texture) { _base_color_texture = texture; }
	std::shared_ptr<Texture> get_base_color_texture() const { return _base_color_texture; }

	void set_metallic_roughness_texture(std::shared_ptr<Texture> texture) { _metallic_roughness_texture = texture; }
	std::shared_ptr<Texture> get_metallic_roughness_texture() const { return _metallic_roughness_texture; }

	void set_normal_texture(std::shared_ptr<Texture> texture) { _normal_texture = texture; }
	std::shared_ptr<Texture> get_normal_texture() const { return _normal_texture; }

	void set_emissive_texture(std::shared_ptr<Texture> texture) { _emissive_texture = texture; }
	std::shared_ptr<Texture> get_emissive_texture() const { return _emissive_texture; }
};

} //namespace feather
