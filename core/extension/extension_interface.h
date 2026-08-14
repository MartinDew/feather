#pragma once

#include "feather_interface.h"

namespace feather {

// Engine-internal: the FeatherGetProcAddress handed to a plugin's
// feather_extension_init. Not itself resolved through get_proc_address.
FeatherProc feather_get_proc_address(const char* name);

// Engine-internal: the ECS names' half of the lookup table, defined in
// extension_ecs.cpp -- kept separate so that file is the only one that
// needs <flecs.h>. feather_get_proc_address() falls back to this.
FeatherProc feather_get_ecs_proc_address(const char* name);

} // namespace feather
