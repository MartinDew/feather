#pragma once

#include "renderer.h"

#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "null_renderer.gen.h"
#endif

namespace feather {

// Renderer used in headless mode. Satisfies RenderingServer's contract without
// creating a GPU device, so a dedicated server needs neither a display nor a
// graphical driver.
class NullRenderer final : public Renderer {
	FCLASS();

protected:
	void _render_scene(RenderScene capture) override {}
	void _on_resize() override {}
};

} //namespace feather
