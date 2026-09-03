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

public:
	// Public to match Renderer, which declares it public and reflected;
	// narrowing it here only blocked calls made through a NullRenderer
	// directly, since going through a Renderer& was always allowed.
	void _render_scene(RenderScene capture) override {}

protected:
	void _on_resize() override {}
};

} //namespace feather
