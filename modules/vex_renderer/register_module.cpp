#include "register_module.h"

#include "vex_renderer.h"

#include <core/rendering/renderer.h>
#include <core/rendering/rendering_server.h>

#include <core/main/class_db.h>

#include "register_vex_renderer_types.gen.h"

namespace feather {

void register_vex_renderer() { register_vex_renderer_types(); }

void unregister_vex_renderer() {}

} //namespace feather