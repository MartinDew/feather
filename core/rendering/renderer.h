#pragma once

#include "mesh_data.h"
#include "render_scene.h"

#include <framework/reflected.h>
#include <framework/reflection_macros.h>
#include <main/engine_settings.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "renderer.gen.h"
#endif

struct SDL_Window;

namespace feather {

class Shader;
class Window;

class FEATHER_API Renderer : public Reflected {
	FCLASS();
	friend class RenderingServer;

protected:
	Renderer();

	virtual void _on_resize() = 0;
	virtual void _compile_shader(Shader& shader) {}

	static SDL_Window* _extract_internal_window(Window& window);

	Window* _window;

public:
	[[method]]
	virtual void _render_scene(RenderScene capture) = 0;
	~Renderer() override = default;
};

} // namespace feather