#include "vex_renderer.h"

#include <Vex/Texture.h>
#include <core/main/engine.h>
#include <core/main/engine_settings.h>
#include <core/main/window.h>
#include <core/math/math_defs.h>
#include <core/rendering/render_data.h>
#include <core/resources/material.h>
#include <core/resources/shader.h>
#include <core/resources/texture.h>
#include <core/world/components/light.h>
#include <framework/assert.h>
#include <framework/bytes.h>

#include <raw_resources/shaders/depth_prepass.slang.gen.h>
#include <raw_resources/shaders/pbr_forward.slang.gen.h>
#include <raw_resources/shaders/shadow_depth.slang.gen.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace feather {

DEFINE_RENDER_DATA_TYPES(vex::BindlessHandle);

using namespace vex;

vex::PlatformWindowHandle VexRenderer::_create_vex_window(Window& window) {
	Window& engine_window = Engine::get().get_main_window();
	SDL_Window* internal_window = Renderer::_extract_internal_window(engine_window);

	PlatformWindowHandle vex_window;
	auto pid = SDL_GetWindowProperties(internal_window);
#if (__linux__)
	if (SDL_HasProperty(pid, SDL_PROP_WINDOW_X11_WINDOW_NUMBER)) {
		vex_window = { PlatformWindowHandle::X11Handle {
				.window = static_cast<::Window>(SDL_GetNumberProperty(pid, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, {})),
				.display = static_cast<Display*>(
						SDL_GetPointerProperty(pid, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr)
				),
		} };
	}
	else if (SDL_HasProperty(pid, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER)) {
		vex_window = { PlatformWindowHandle::WaylandHandle {
				.window = static_cast<::wl_surface*>(
						SDL_GetPointerProperty(pid, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr)
				),
				.display = static_cast<::wl_display*>(
						SDL_GetPointerProperty(pid, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr)
				) } };
	}
	else {
		fassert(false, "VexRenderer : No supported video driver found");
	}
#elif (_WIN32)
	using NativeWindow = HWND;
	vex_window = { PlatformWindowHandle::WindowsHandle { .window = static_cast<NativeWindow>(SDL_GetPointerProperty(
																 pid, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr
														 )) } };

#elifdef __APPLE__
	// macOS implementation would go here
#endif

	return vex_window;
}

void VexRenderer::_bind_members() {
	ClassDB::bind_property(&Type::_use_reverse_z, "use_reverse_z");
}

static const std::filesystem::path shader_path = std::filesystem::current_path() / "shaders";

VexRenderer::VexRenderer()
		: graphics(
				  vex::GraphicsCreateDesc {
						  .platformWindow = { .windowHandle = _create_vex_window(Engine::get().get_main_window()),
											  .width = static_cast<uint32_t>(
													  Engine::get().get_main_window().properties.width
											  ),
											  .height = static_cast<uint32_t>(
													  Engine::get().get_main_window().properties.height
											  ) },
						  .enableGPUDebugLayer = !VEX_SHIPPING,
						  .enableGPUBasedValidation = !VEX_SHIPPING,
				  }
		  )
		, _use_reverse_z { true } {
	auto main_window = Engine::get().get_main_window();
	uint32_t width = main_window.properties.width;
	uint32_t height = main_window.properties.height;

	// Depth texture
	depthTexture = graphics.CreateTexture(
			{
					.name = "Depth Texture",
					.type = vex::TextureType::Texture2D,
					.format = vex::TextureFormat::D32_FLOAT,
					.width = width,
					.height = height,
					.usage = vex::TextureUsage::DepthStencil | TextureUsage::ShaderRead,
					.clearValue =
							vex::TextureClearValue {
									.depth = _use_reverse_z ? 0.0f : 1.0f,
							},
			}
	);

	vex::CommandContext ctx = graphics.CreateCommandContext(vex::QueueType::Graphics);

	// Create GPU buffers
	_camera_uniform_buffer = graphics.CreateBuffer(
			vex::BufferDesc::CreateUniformBufferDesc("Camera Uniforms", sizeof(CameraBufferData))
	);
	_lights_structured_buffer =
			graphics.CreateBuffer(vex::BufferDesc::CreateGenericBufferDesc("Lights Buffer", sizeof(LightBufferData)));

	_per_entity_uniform_buffer = graphics.CreateBuffer(
			{
					.name = "Per-Entity Uniform",
					.byteSize = sizeof(InstanceBufferData),
					.usage = vex::BufferUsage::ShaderReadUniform,
					.memoryLocality = vex::ResourceMemoryLocality::GPUOnly,
			}
	);

	_material_buffer = graphics.CreateBuffer(
			{
					.name = "Material Buffer",
					.byteSize = sizeof(PbrMaterialBufferData),
					.usage = vex::BufferUsage::ShaderReadUniform,
					.memoryLocality = vex::ResourceMemoryLocality::GPUOnly,
			}
	);

	// Create default textures
	// White 1x1 texture
	{
		std::array<uint8_t, 4> whitePixel = { 255, 255, 255, 255 };
		_default_white_texture = graphics.CreateTexture(
				{ .name = "Default White",
				  .type = vex::TextureType::Texture2D,
				  .format = vex::TextureFormat::RGBA8_UNORM,
				  .width = 1,
				  .height = 1,
				  .usage = vex::TextureUsage::ShaderRead }
		);
		std::span bytes = to_bytes(whitePixel);
		ctx.EnqueueDataUpload(_default_white_texture, bytes, vex::TextureRegion::SingleMip(0));
		ctx.Barrier(_default_white_texture, RHIBarrierAccess::ShaderRead);
		_default_white_handle = graphics.GetBindlessHandle(
				vex::TextureBinding { .texture = _default_white_texture, .usage = vex::TextureBindingUsage::ShaderRead }
		);
	}

	// Default normal map (0.5, 0.5, 1.0, 1.0)
	{
		std::array<uint8_t, 4> normalPixel = { 128, 128, 255, 255 };
		_default_normal_texture = graphics.CreateTexture(
				{ .name = "Default Normal",
				  .type = vex::TextureType::Texture2D,
				  .format = vex::TextureFormat::RGBA8_UNORM,
				  .width = 1,
				  .height = 1,
				  .usage = vex::TextureUsage::ShaderRead }
		);
		ctx.EnqueueDataUpload(_default_normal_texture, to_bytes(normalPixel), vex::TextureRegion::SingleMip(0));
		ctx.Barrier(_default_normal_texture, RHIBarrierAccess::ShaderRead);
		_default_normal_handle = graphics.GetBindlessHandle(
				vex::TextureBinding { .texture = _default_normal_texture,
									  .usage = vex::TextureBindingUsage::ShaderRead }
		);
	}

	// Default metallic/roughness (0, 1, 0, 0) - non-metallic, rough
	{
		std::array<uint8_t, 4> mrPixel = { 0, 255, 0, 0 };
		_default_metallic_roughness_texture = graphics.CreateTexture(
				{ .name = "Default Metallic/Roughness",
				  .type = vex::TextureType::Texture2D,
				  .format = vex::TextureFormat::RGBA8_UNORM,
				  .width = 1,
				  .height = 1,
				  .usage = vex::TextureUsage::ShaderRead }
		);
		ctx.EnqueueDataUpload(_default_metallic_roughness_texture, to_bytes(mrPixel), vex::TextureRegion::SingleMip(0));
		ctx.Barrier(_default_metallic_roughness_texture, RHIBarrierAccess::ShaderRead);
		_default_mr_handle = graphics.GetBindlessHandle(
				vex::TextureBinding { .texture = _default_metallic_roughness_texture,
									  .usage = vex::TextureBindingUsage::ShaderRead }
		);
	}

	// Setup samplers
	std::array<vex::StaticTextureSampler, 3> samplers {
		vex::StaticTextureSampler::CreateSampler(vex::FilterMode::Linear, vex::AddressMode::Clamp),
		vex::StaticTextureSampler::CreateSampler(vex::FilterMode::Point, vex::AddressMode::Clamp),
		[&] {
			vex::StaticTextureSampler s;
			s.minFilter = FilterMode::Linear;
			s.magFilter = FilterMode::Linear;
			s.addressU = vex::AddressMode::Clamp;
			s.addressV = vex::AddressMode::Clamp;
			s.compareOp = _use_reverse_z ? vex::CompareOp::GreaterEqual : vex::CompareOp::Less;
			return s;
		}(),
	};

	graphics.SetStaticSamplers(samplers);

	// Initialize shader compiler with the shader directory on the include path
	// (needed for Slang import resolution even when compiling from embedded source)
	_shader_compiler = vex::ShaderCompiler(
			vex::ShaderCompilerSettings {
					.shaderIncludeDirectories = { shader_path },
			}
	);
	_compile_engine_shaders();
	_build_draw_descs();

	graphics.Submit(ctx);
}

void VexRenderer::_render_scene(const RenderScene capture) {
	auto ctx = graphics.CreateCommandContext(vex::QueueType::Graphics);
	// Upload camera uniforms
	_upload_camera_uniforms(capture, ctx);

	// Check if there are any shadow-casting lights
	bool hasShadows = false;
	for (const auto& light : capture.get_lights()) {
		if (light.cast_shadows) {
			hasShadows = true;
			break;
		}
	}

	{
		VEX_GPU_SCOPED_EVENT(ctx, "Depth Pre-Pass");
		_render_depth_pre_pass(capture, ctx);
	}

	if (hasShadows) {
		VEX_GPU_SCOPED_EVENT(ctx, "Shadow Pass");
		_render_shadow_pass(capture, ctx);
	}

	{
		VEX_GPU_SCOPED_EVENT(ctx, "Forward Pass");
		_render_forward_pass(capture, ctx);
	}

	graphics.Submit(ctx);
	graphics.Present();
}

void VexRenderer::_on_resize() {
	_shader_compiler.RecompileChangedShaders();
	_shader_draw_desc_cache.clear();
	_build_draw_descs();

	auto width = _window->properties.width;
	auto height = _window->properties.height;

	if (width == 0 || height == 0) {
		return;
	}

	// Recreate depth texture
	depthTexture = graphics.CreateTexture(
			{
					.name = "Depth Texture",
					.type = vex::TextureType::Texture2D,
					.format = vex::TextureFormat::D32_FLOAT,
					.width = static_cast<uint32_t>(width),
					.height = static_cast<uint32_t>(height),
					.usage = vex::TextureUsage::DepthStencil | TextureUsage::ShaderRead,
					.clearValue =
							vex::TextureClearValue {
									.depth = _use_reverse_z ? 0.0f : 1.0f,
							},
			}
	);

	graphics.OnWindowResized(width, height);
}

void VexRenderer::_compile_engine_shaders() {
	// For each engine shader file, prefer the real file on disk (enables hot-reload and
	// developer override), falling back to the embedded source when no file exists.
	auto get_filepath = [&](const char* filename) -> std::string {
		auto real_path = shader_path / filename;
		if (std::filesystem::exists(real_path)) {
			return real_path.string();
		}
		return std::string("engine://shaders/") + filename;
	};

	auto compile = [&](const std::string& filepath,
					   std::string_view entry_point,
					   vex::ShaderType type,
					   const std::span<const uint8_t> embedded_src) {
		fassert(embedded_src.size() > 0, std::format("Embedded shader source is null for {}", filepath));
		vex::ShaderKey key {
			.filepath = filepath,
			.entryPoint = std::string(entry_point),
			.type = type,
			.compiler = vex::ShaderCompilerBackend::Slang,
		};
		if (filepath.starts_with("engine://")) {
			std::string_view content = reinterpret_cast<const char*>(embedded_src.data());
			_shader_compiler.CompileShaderFromSourceCode(key, content);
		}
		else {
			_shader_compiler.CompileShaderFromFilepath(key);
		}
	};

	_depth_prepass_path = get_filepath("depth_prepass.slang");
	compile(_depth_prepass_path, "Vertex", vex::ShaderType::VertexShader, shaders_depth_prepass_slang);
	compile(_depth_prepass_path, "Pixel", vex::ShaderType::PixelShader, shaders_depth_prepass_slang);

	_pbr_forward_path = get_filepath("pbr_forward.slang");
	compile(_pbr_forward_path, "VSMain", vex::ShaderType::VertexShader, shaders_pbr_forward_slang);
	compile(_pbr_forward_path, "PSMain", vex::ShaderType::PixelShader, shaders_pbr_forward_slang);

	_shadow_depth_path = get_filepath("shadow_depth.slang");
	compile(_shadow_depth_path, "VSMain", vex::ShaderType::VertexShader, shaders_shadow_depth_slang);
	compile(_shadow_depth_path, "PSMain", vex::ShaderType::PixelShader, shaders_shadow_depth_slang);
}

void VexRenderer::_build_draw_descs() {
	vex::VertexInputLayout pbrVertexLayout {
		.attributes = {
			{
				.semanticName = "POSITION",
				.semanticIndex = 0,
				.binding = 0,
				.format = vex::TextureFormat::RGB32_FLOAT,
				.offset = 0,
			},
			{
				.semanticName = "NORMAL",
				.semanticIndex = 0,
				.binding = 0,
				.format = vex::TextureFormat::RGB32_FLOAT,
				.offset = sizeof(float) * 3,
			},
			{
				.semanticName = "TEXCOORD",
				.semanticIndex = 0,
				.binding = 0,
				.format = vex::TextureFormat::RG32_FLOAT,
				.offset = sizeof(float) * 6,
			},
		},
		.bindings = {
			{
				.binding = 0,
				.strideByteSize = static_cast<uint32_t>(sizeof(Vertex)),
				.inputRate = vex::VertexInputLayout::InputRate::PerVertex,
			},
		},
	};

	vex::DepthStencilState depthStencilState {
		.depthTestEnabled = true,
		.depthWriteEnabled = true,
		.depthCompareOp = _use_reverse_z ? vex::CompareOp::GreaterEqual : vex::CompareOp::Less,
		.minDepthBounds = _use_reverse_z ? 1.0f : 0.0f,
		.maxDepthBounds = _use_reverse_z ? 0.0f : 1.0f,
	};

	auto get_view = [&](const std::string& filepath, std::string_view entry_point, vex::ShaderType type) {
		return _shader_compiler.GetShaderView(
				{
						.filepath = filepath,
						.entryPoint = std::string(entry_point),
						.type = type,
						.compiler = vex::ShaderCompilerBackend::Slang,
				}
		);
	};

	_depth_pre_pass_desc = vex::DrawDesc {
		.vertexShader = get_view(_depth_prepass_path, "Vertex", vex::ShaderType::VertexShader),
		.pixelShader = get_view(_depth_prepass_path, "Pixel", vex::ShaderType::PixelShader),
		.vertexInputLayout = pbrVertexLayout,
		.rasterizerState = {},
		.depthStencilState = depthStencilState,
	};

	_pbr_draw_desc = vex::DrawDesc {
		.vertexShader = get_view(_pbr_forward_path, "VSMain", vex::ShaderType::VertexShader),
		.pixelShader  = get_view(_pbr_forward_path, "PSMain", vex::ShaderType::PixelShader),
		.vertexInputLayout = pbrVertexLayout,
		.rasterizerState = {
			.cullMode = vex::CullMode::Back,
			.depthBiasEnabled = true,
			.depthBiasConstantFactor = -0.9f,
			.depthBiasClamp = -1.0f,
		},
		.depthStencilState = depthStencilState,
	};

	_shadow_draw_desc = vex::DrawDesc {
		.vertexShader = get_view(_shadow_depth_path, "VSMain", vex::ShaderType::VertexShader),
		.pixelShader = get_view(_shadow_depth_path, "PSMain", vex::ShaderType::PixelShader),
		.vertexInputLayout = pbrVertexLayout,
		.depthStencilState = depthStencilState,
	};
}

std::string VexRenderer::_get_shader_virtual_path(const Shader& shader) const {
	if (shader.is_path_based())
		return std::string(shader.get_source());
	auto resource_path = shader.get_path();
	return resource_path.empty() ? std::string("user://shaders/") + std::to_string(reinterpret_cast<uintptr_t>(&shader))
								 : std::string("user://shaders/") + resource_path.string();
}

void VexRenderer::_compile_shader(Shader& shader) {
	if (!shader.is_valid())
		return;

	std::string filepath = _get_shader_virtual_path(shader);
	auto compile = [&](std::string_view entry, ShaderType type) {
		ShaderKey key {
			.filepath = filepath,
			.entryPoint = std::string(entry),
			.type = type,
			.compiler = ShaderCompilerBackend::Slang,
		};
		if (shader.is_source_based())
			_shader_compiler.CompileShaderFromSourceCode(key, shader.get_source());
		else
			_shader_compiler.CompileShaderFromFilepath(key);
	};

	compile(shader.get_vertex_entry(), ShaderType::VertexShader);
	compile(shader.get_pixel_entry(), ShaderType::PixelShader);
}

vex::DrawDesc& VexRenderer::_get_or_build_shader_draw_desc(Shader& shader) {
	auto it = _shader_draw_desc_cache.find(&shader);
	if (it != _shader_draw_desc_cache.end())
		return it->second;

	_compile_shader(shader);

	std::string filepath = _get_shader_virtual_path(shader);
	auto get_view = [&](std::string_view entry, ShaderType type) {
		ShaderKey key { .filepath = filepath,
						.entryPoint = std::string(entry),
						.type = type,
						.compiler = ShaderCompilerBackend::Slang };
		return _shader_compiler.GetShaderView(key);
	};

	vex::DrawDesc desc = _pbr_draw_desc;
	desc.vertexShader = get_view(shader.get_vertex_entry(), ShaderType::VertexShader);
	desc.pixelShader = get_view(shader.get_pixel_entry(), ShaderType::PixelShader);

	return _shader_draw_desc_cache.emplace(&shader, std::move(desc)).first->second;
}

void VexRenderer::_render_depth_pre_pass(const RenderScene& capture, vex::CommandContext& ctx) {
	// Clear and set up depth target
	ctx.ClearTexture(depthTexture);
	ctx.SetViewport(0, 0, _window->properties.width, _window->properties.height);
	ctx.SetScissor(0, 0, _window->properties.width, _window->properties.height);

	std::array<ResourceBinding, 1> bindings {
		BufferBinding { .buffer = _camera_uniform_buffer, .usage = BufferBindingUsage::UniformBuffer },
	};

	auto handles = graphics.GetBindlessHandles(bindings);
	ConstantBinding constant_bindings { std::span(handles) };

	const auto& entities = capture.get_entities();
	for (const auto& entity : entities) {
		auto& meshBuffers = _get_or_create_mesh_buffers(entity.triangle_mesh, ctx);

		vex::BufferBinding vertexBufferBinding {
			.buffer = meshBuffers.vertex_buffer,
			.strideByteSize = static_cast<uint32_t>(sizeof(Vertex)),
		};
		vex::BufferBinding indexBufferBinding {
			.buffer = meshBuffers.index_buffer,
			.strideByteSize = static_cast<uint32_t>(sizeof(uint32_t)),
		};

		ctx.DrawIndexed(
				_depth_pre_pass_desc,
				{
						.depthStencil = vex::TextureBinding(depthTexture),
						.vertexBuffers = { &vertexBufferBinding, 1 },
						.indexBuffer = indexBufferBinding,
				},
				constant_bindings,
				bindings,
				meshBuffers.index_count
		);
	}
}

// Shadow pass implementation
void VexRenderer::_render_shadow_pass(const RenderScene& capture, vex::CommandContext& ctx) {
	const auto& lights = capture.get_lights();
	_light_to_shadow_map_index.clear();
	_light_view_proj_cache.clear();

	size_t shadow_map_index = 0;
	for (size_t i = 0; i < lights.size(); ++i) {
		const auto& light = lights[i];
		if (!light.cast_shadows)
			continue;

		static constexpr size_t w = 1024 * 2;
		static constexpr size_t h = 1024 * 2;
		// Ensure we have a shadow map
		if (i >= _shadow_maps.size()) {
			_shadow_maps.push_back(graphics.CreateTexture(
					{
							.name = "Shadow Map",
							.type = vex::TextureType::Texture2D,
							.format = vex::TextureFormat::D32_FLOAT,
							.width = w,
							.height = h,
							.usage = vex::TextureUsage::DepthStencil | vex::TextureUsage::ShaderRead,
							.clearValue =
									vex::TextureClearValue {
											.depth = _use_reverse_z ? 0.0f : 1.0f,
									},
					}
			));
		}

		vex::Texture& shadow_map = _shadow_maps[i];
		auto shadow_map_binding = vex::TextureBinding(shadow_map, TextureBindingUsage::ShaderRead);

		_light_to_shadow_map_index[static_cast<uint32_t>(i)] = graphics.GetBindlessHandle(shadow_map_binding);

		// Compute light view-projection matrix and cache it for _upload_lights_buffer
		const Matrix lightVP = _compute_light_view_proj(light, capture);
		_light_view_proj_cache[static_cast<uint32_t>(i)] = lightVP;

		// Set render target (depth-only)
		ctx.ClearTexture(shadow_map);
		ctx.SetViewport(0, 0, w, h);
		ctx.SetScissor(0, 0, w, h);

		// Render entities
		const auto& entities = capture.get_entities();
		for (const auto& entity : entities) {
			if (!entity.cast_shadows)
				continue;

			// Get mesh buffers
			auto& meshBuffers = _get_or_create_mesh_buffers(entity.triangle_mesh, ctx);

			// Upload uniforms (MVP matrix from light's perspective)
			Matrix model = entity.transform.to_matrix_with_scale();
			Matrix mvp = model * lightVP;

			// Draw
			vex::BufferBinding vertexBufferBinding {
				.buffer = meshBuffers.vertex_buffer,
				.strideByteSize = static_cast<uint32_t>(sizeof(Vertex)),
			};
			vex::BufferBinding indexBufferBinding {
				.buffer = meshBuffers.index_buffer,
				.strideByteSize = static_cast<uint32_t>(sizeof(uint32_t)),
			};

			ctx.DrawIndexed(
					_shadow_draw_desc,
					{
							.depthStencil = TextureBinding { shadow_map },
							.vertexBuffers = { &vertexBufferBinding, 1 },
							.indexBuffer = indexBufferBinding,
					},
					vex::ConstantBinding(mvp),
					{},
					meshBuffers.index_count
			);
		}
	}
}

void VexRenderer::_render_forward_pass(const RenderScene& capture, vex::CommandContext& ctx) {
	// Clear back buffer and depth
	auto backBuffer = graphics.GetCurrentPresentTexture();
	ctx.ClearTexture(backBuffer);

	ctx.SetViewport(0, 0, _window->properties.width, _window->properties.height);
	ctx.SetScissor(0, 0, _window->properties.width, _window->properties.height);

	_upload_lights_buffer(capture, ctx);

	// Render entities
	const auto& entities = capture.get_entities();
	for (const auto& entity : entities) {
		auto& meshBuffers = _get_or_create_mesh_buffers(entity.triangle_mesh, ctx);

		vex::DrawDesc* draw_desc = &_pbr_draw_desc;
		if (const auto* shaderMat = object_cast<const ShaderMaterial>(entity.material.get())) {
			if (auto shader = shaderMat->get_shader(); shader && shader->is_valid())
				draw_desc = &_get_or_build_shader_draw_desc(*shader);
		}

		const PBRMaterial* pbrMat = object_cast<const PBRMaterial>(entity.material.get());
		if (!pbrMat) {
			static PBRMaterial defaultMat;
			pbrMat = &defaultMat;
		}

		// Get texture handles
		vex::BindlessHandle baseColorHandle =
				_get_texture_handle(pbrMat->get_base_color_texture().get(), ctx, _default_white_handle);
		vex::BindlessHandle metallicRoughnessHandle =
				_get_texture_handle(pbrMat->get_metallic_roughness_texture().get(), ctx, _default_mr_handle);
		vex::BindlessHandle normalHandle =
				_get_texture_handle(pbrMat->get_normal_texture().get(), ctx, _default_normal_handle);
		vex::BindlessHandle emissiveHandle = _get_texture_handle(pbrMat->get_emissive_texture().get(), ctx, { 0 });

		// Build per-entity uniforms
		InstanceBufferData entity_uniforms;

		entity_uniforms.model = entity.transform.to_matrix_with_scale();
		entity_uniforms.normalMatrix = _compute_normal_matrix(entity_uniforms.model);

		PbrMaterialBufferData materialData;

		materialData.baseColorFactor = pbrMat->get_base_color_factor();
		materialData.metallicFactor = pbrMat->get_metallic_factor();
		materialData.roughnessFactor = pbrMat->get_roughness_factor();
		materialData.emissiveFactor = pbrMat->get_emissive_factor();
		materialData.baseColorHandle = baseColorHandle;
		materialData.metallicRoughnessHandle = metallicRoughnessHandle;
		materialData.normalHandle = normalHandle;
		materialData.emissiveHandle = emissiveHandle;

		ctx.EnqueueDataUpload(_material_buffer, to_bytes(materialData));

		std::vector<ResourceBinding> tracked_bindings;
		for (auto& shadowMap : _shadow_maps) {
			tracked_bindings.push_back(
					TextureBinding { .texture = shadowMap, .usage = TextureBindingUsage::ShaderRead }
			);
		}

		// Draw
		BufferBinding vertexBufferBinding {
			.buffer = meshBuffers.vertex_buffer,
			.strideByteSize = static_cast<uint32_t>(sizeof(Vertex)),
		};
		vex::BufferBinding indexBufferBinding {
			.buffer = meshBuffers.index_buffer,
			.strideByteSize = static_cast<uint32_t>(sizeof(uint32_t)),
		};

		std::array renderTargets = { vex::TextureBinding { .texture = backBuffer } };

		ctx.EnqueueDataUpload(_per_entity_uniform_buffer, to_bytes(entity_uniforms));

		std::array<ResourceBinding, 4> bindings {
			BufferBinding::CreateConstantBuffer(_camera_uniform_buffer),
			BufferBinding::CreateConstantBuffer(_per_entity_uniform_buffer),
			BufferBinding::CreateConstantBuffer(_material_buffer),
			BufferBinding::CreateStructuredBuffer(
					_lights_structured_buffer, sizeof(LightBufferData), 0, capture.get_light_count()
			)
		};

		std::vector<BindlessHandle> handles = graphics.GetBindlessHandles(bindings);
		tracked_bindings.append_range(bindings);
		std::vector<uint32_t> push_data(handles.size());
		std::copy_n(reinterpret_cast<uint32_t*>(handles.data()), handles.size(), push_data.begin());
		push_data.push_back(capture.get_light_count());

		ConstantBinding constant_bindings { std::span(push_data) };
		ctx.DrawIndexed(
				*draw_desc,
				{
						.renderTargets = renderTargets,
						.depthStencil = vex::TextureBinding(depthTexture),
						.vertexBuffers = { &vertexBufferBinding, 1 },
						.indexBuffer = indexBufferBinding,
				},
				constant_bindings,
				tracked_bindings,
				meshBuffers.index_count
		);
	}
}

void VexRenderer::_upload_camera_uniforms(const RenderScene& capture, vex::CommandContext& ctx) const {
	const auto& transform = capture.get_camera_transform();
	const auto& projection =
			_use_reverse_z ? capture.get_camera_projection().create_reverse_z() : capture.get_camera_projection();

	Matrix view = transform.to_matrix_no_scale().invert(); // World-to-camera
	Matrix proj = projection.get_matrix();
	Matrix viewProj = view * proj;

	CameraBufferData uniforms;

	uniforms.viewProj = viewProj;
	uniforms.cameraPos = transform.position;

	std::span<const std::byte> bytes = to_bytes(uniforms);
	ctx.EnqueueDataUpload(_camera_uniform_buffer, bytes);
}

void VexRenderer::_upload_lights_buffer(const RenderScene& capture, vex::CommandContext& ctx) {
	const auto& lights = capture.get_lights();

	std::vector<LightBufferData> gpuLights;
	for (size_t i = 0; i < lights.size(); ++i) {
		const auto& light = lights[i];
		LightBufferData gpuLight {};
		gpuLight.type = static_cast<uint32_t>(light.type);
		gpuLight.position = light.position;
		gpuLight.direction = light.direction;
		gpuLight.color = Color(light.color.x, light.color.y, light.color.z, light.intensity);
		gpuLight.range = light.range;
		gpuLight.spotAngleCos = std::cos(deg_to_rad(light.spot_angle));
		auto vp_it = _light_view_proj_cache.find(static_cast<uint32_t>(i));
		gpuLight.viewProj =
				(vp_it != _light_view_proj_cache.end()) ? vp_it->second : _compute_light_view_proj(light, capture);

		// Shadow map index
		auto it = _light_to_shadow_map_index.find(static_cast<uint32_t>(i));
		gpuLight.shadowMapHandle = (it != _light_to_shadow_map_index.end()) ? it->second : vex::GInvalidBindlessHandle;
		gpuLight.shadowBias = 0.0001f;

		gpuLights.push_back(gpuLight);
	}

	if (!gpuLights.empty()) {
		auto bytes = std::as_bytes(std::span(gpuLights));

		if (_lights_structured_buffer.desc.byteSize != bytes.size()) {
			auto old_buffer = _lights_structured_buffer;
			_lights_structured_buffer =
					graphics.CreateBuffer(vex::BufferDesc::CreateGenericBufferDesc("Lights Buffer", bytes.size()));

			graphics.DestroyBuffer(old_buffer);
		}

		ctx.EnqueueDataUpload(_lights_structured_buffer, bytes);
	}
}

VexRenderer::MeshBuffers&
VexRenderer::_get_or_create_mesh_buffers(const std::shared_ptr<MeshData>& mesh, vex::CommandContext& ctx) {
	auto it = _mesh_cache.find(mesh);
	if (it != _mesh_cache.end()) {
		return it->second;
	}

	// Create vertex buffer
	const auto& vertices = mesh->get_vertices();
	vex::Buffer vb =
			graphics.CreateBuffer(vex::BufferDesc::CreateVertexBufferDesc("Mesh VB", sizeof(Vertex) * vertices.size()));
	ctx.EnqueueDataUpload(vb, std::as_bytes(std::span<Vertex>(vertices)));

	// Create index buffer
	const auto& indices = mesh->get_indices();
	vex::Buffer ib =
			graphics.CreateBuffer(vex::BufferDesc::CreateIndexBufferDesc("Mesh IB", sizeof(uint32_t) * indices.size()));
	ctx.EnqueueDataUpload(ib, std::as_bytes(std::span<Index>(indices)));

	ctx.Barrier(vb, RHIBarrierAccess::MemoryRead);
	ctx.Barrier(ib, RHIBarrierAccess::MemoryRead);

	MeshBuffers buffers { vb, ib, static_cast<uint32_t>(indices.size()) };
	_mesh_cache[mesh] = buffers;
	return _mesh_cache[mesh];
}

VexRenderer::TextureGPUData& VexRenderer::_get_or_create_texture(const Texture* texture, vex::CommandContext& ctx) {
	auto it = _texture_cache.find(texture);
	if (it != _texture_cache.end()) {
		return it->second;
	}

	// TODO: Implement actual texture loading from Texture resource
	// For now, return default white texture data
	TextureGPUData gpuData { _default_white_texture, _default_white_handle };
	_texture_cache[texture] = gpuData;
	return _texture_cache[texture];
}

vex::BindlessHandle
VexRenderer::_get_texture_handle(const Texture* texture, vex::CommandContext& ctx, vex::BindlessHandle default_handle) {
	if (!texture) {
		return default_handle;
	}

	auto& gpuData = _get_or_create_texture(texture, ctx);
	return gpuData.bindless_handle;
}

Matrix VexRenderer::_compute_light_view_proj(const Light& light, const RenderScene& capture) const {
	if (light.type == Light::Type::Directional) {
		Vector3 sceneCenter = _compute_scene_center(capture);
		float sceneRadius = _compute_scene_radius(capture, sceneCenter);

		Vector3 lightPos = sceneCenter - light.direction; // * (sceneRadius * 2.0f);
		Matrix view = Matrix::create_look_at(lightPos, sceneCenter, Vector3(0, 1, 0));
		auto nearFar = std::make_pair(-sceneRadius * 4.0f, sceneRadius * 4.0f);
		if (_use_reverse_z) {
			std::swap(nearFar.first, nearFar.second);
		}
		Matrix proj =
				Matrix::create_orthographic(sceneRadius * 2.0f, sceneRadius * 2.0f, nearFar.first, nearFar.second);
		return view * proj;
	}

	if (light.type == Light::Type::Spot) {
		Matrix view = Matrix::create_look_at(light.position, light.position + light.direction, Vector3(0, 1, 0));
		float fov = light.spot_angle * 2.0f;
		Matrix proj = Matrix::create_perspective_field_of_view(deg_to_rad(fov), 1.0f, 0.1f, light.range);
		return view * proj;
	}

	// Point lights not implemented (requires cubemap)
	return Matrix::identity;
}

Vector3 VexRenderer::_compute_scene_center(const RenderScene& capture) {
	const auto& entities = capture.get_entities();
	if (entities.size() == 0)
		return Vector3::zero;

	Vector3 sum = Vector3::zero;
	for (const auto& entity : entities) {
		sum += entity.transform.position;
	}
	return sum / static_cast<float>(entities.size());
}

float VexRenderer::_compute_scene_radius(const RenderScene& capture, const Vector3& center) {
	const auto& entities = capture.get_entities();
	float maxDist = 10.0f;

	for (const auto& entity : entities) {
		float dist = Vector3::distance(center, entity.transform.position);
		// TODO: Add mesh bounding sphere radius
		maxDist = std::max(maxDist, dist + 10.0f);
	}
	return maxDist;
}

Matrix VexRenderer::_compute_normal_matrix(const Matrix& modelMatrix) {
	Matrix inv = modelMatrix.invert();
	return inv.transpose();
}

} //namespace feather
