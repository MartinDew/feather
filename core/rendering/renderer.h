#pragma once

#include "mesh_data.h"
#include "render_scene.h"

#include <framework/reflected.h>
#include <framework/reflection_macros.h>
#include <main/engine_settings.h>

struct SDL_Window;

namespace feather {

class Shader;
class Window;

class Renderer : public Reflected {
	FCLASS(Renderer, Reflected)
	friend class RenderingServer;

protected:
	Renderer();

	virtual void _on_resize() = 0;
	virtual void _compile_shader(Shader& shader) {}

	static SDL_Window* _extract_internal_window(Window& window);

	Window* _window;

	static void _bind_members();

public:
	virtual void _render_scene(RenderScene capture) = 0;
	~Renderer() override = default;
};

} // namespace feather