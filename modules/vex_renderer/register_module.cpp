#include "register_module.h"

#include "vex_renderer.h"

#include <core/rendering/renderer.h>
#include <core/rendering/rendering_server.h>

#include <core/main/class_db.h>

#include "register_vex_renderer_types.gen.h"

namespace feather {

void register_vex_renderer(InitLevel level) {
	// Reflected types only, so nothing beyond ClassDB has to be up yet.
	if (level != InitLevel::Core) {
		return;
	}

	register_vex_renderer_types();
}

void unregister_vex_renderer(InitLevel level) {
}

} //namespace feather
