#pragma once

#include "feather_interface.h"

namespace feather {

// Engine-internal: the FeatherGetProcAddress handed to a plugin's
// feather_extension_init. Not itself resolved through get_proc_address.
FeatherProc feather_get_proc_address(const char* name);

} // namespace feather
