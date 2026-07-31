#include "renderer.h"

#include "framework/functions.h"
#include "framework/reflection_macros.h"
#include "main/class_db.h"
#include "main/engine.h"
#include "main/window.h"
#include "rendering/mesh_data.h"

namespace feather {

Renderer::Renderer() : _window(&Engine::get().get_main_window()) {
}

SDL_Window* Renderer::_extract_internal_window(Window& window) {
	return window._internal_window;
}

} //namespace feather