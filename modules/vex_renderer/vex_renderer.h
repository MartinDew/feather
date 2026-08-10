#pragma once

#include <core/framework/reflection_macros.h>
#include <core/math/math_defs.h>
#include <core/rendering/render_scene.h>
#include <core/rendering/renderer.h>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include <Vex.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "vex_renderer.gen.h"
#endif

namespace feather {

class Texture;
class MeshData;

// Never named directly by a project DLL -- selected internally via ClassDB
// (like NullRenderer) rather than something plugin code constructs or
// references by type. FEATHER_INTERNAL, not FEATHER_API.
class FEATHER_INTERNAL VexRenderer : public Renderer {
	FCLASS();

	// Core rendering resources
	vex::Texture depthTexture;
	vex::Graphics graphics;
	vex::ShaderCompiler _shader_compiler;

	// Shadow maps
	std::vector<vex::Texture> _shadow_maps;
	std::unordered_map<uint32_t, vex::BindlessHandle> _light_to_shadow_map_index;
	std::unordered_map<uint32_t, Matrix> _light_view_proj_cache;

	// GPU buffers
	vex::Buffer _camera_uniform_buffer;
	vex::Buffer _lights_structured_buffer;
	vex::Buffer _per_entity_uniform_buffer;
	vex::Buffer _material_buffer;

	// Resource caches
	struct MeshBuffers {
		vex::Buffer vertex_buffer;
		vex::Buffer index_buffer;
		uint32_t index_count = 0;
	};
	struct TextureGPUData {
		vex::Texture texture;
		vex::BindlessHandle bindless_handle;
	};
	std::unordered_map<std::shared_ptr<const MeshData>, MeshBuffers> _mesh_cache;
	std::unordered_map<const Texture*, TextureGPUData> _texture_cache;
	std::unordered_map<const Shader*, vex::DrawDesc> _shader_draw_desc_cache;

	// Default textures
	vex::Texture _default_white_texture;
	vex::Texture _default_normal_texture;
	vex::Texture _default_metallic_roughness_texture;
	vex::BindlessHandle _default_white_handle;
	vex::BindlessHandle _default_normal_handle;
	vex::BindlessHandle _default_mr_handle;

	// Shader pipeline states
	vex::DrawDesc _depth_pre_pass_desc;
	vex::DrawDesc _pbr_draw_desc;
	vex::DrawDesc _shadow_draw_desc;

	// Resolved shader filepaths (real path or "engine://shaders/..." virtual path)
	std::string _depth_prepass_path;
	std::string _pbr_forward_path;
	std::string _shadow_depth_path;

	[[get(public), set(public)]]
	bool _use_reverse_z;

	// Shader setup
	void _compile_engine_shaders();
	void _build_draw_descs();
	void _compile_shader(Shader& shader) override;
	std::string _get_shader_virtual_path(const Shader& shader) const;
	vex::DrawDesc& _get_or_build_shader_draw_desc(Shader& shader);

	// Helper methods
	void _render_depth_pre_pass(const RenderScene& capture, vex::CommandContext& ctx);
	void _render_shadow_pass(const RenderScene& capture, vex::CommandContext& ctx);
	void _render_forward_pass(const RenderScene& capture, vex::CommandContext& ctx);
	void _upload_camera_uniforms(const RenderScene& capture, vex::CommandContext& ctx) const;
	void _upload_lights_buffer(const RenderScene& capture, vex::CommandContext& ctx);
	MeshBuffers& _get_or_create_mesh_buffers(const std::shared_ptr<MeshData>& mesh, vex::CommandContext& ctx);
	TextureGPUData& _get_or_create_texture(const Texture* texture, vex::CommandContext& ctx);
	vex::BindlessHandle
	_get_texture_handle(const Texture* texture, vex::CommandContext& ctx, vex::BindlessHandle default_handle);
	Matrix _compute_light_view_proj(const Light& light, const RenderScene& capture) const;

	// Utility methods
	static Vector3 _compute_scene_center(const RenderScene& capture);
	static float _compute_scene_radius(const RenderScene& capture, const Vector3& center);
	static Matrix _compute_normal_matrix(const Matrix& modelMatrix);

	static vex::PlatformWindowHandle _create_vex_window(Window& window);

protected:
	void _render_scene(RenderScene capture) override;
	void _on_resize() override;

public:
	VexRenderer();
};

} //namespace feather
