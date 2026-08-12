#pragma once

#include <resources/extension_abi.h>

#include <cstddef>

namespace feather {

// The engine's real entry point. The plain dev/editor feather.exe's own
// main() (feather_main.cpp, bottom of the file) is a thin call to this with
// no statics; a static shipping executable (plugin-abi-rework plan, Stage 7)
// gets its own thin main() -- generated alongside static_extensions.gen.cpp
// by feather_static_registry.lua, see tools/SDK/FeatherSDK.lua's
// feather_game_target() -- that calls this instead with its own statically
// linked-in extensions, bypassing ExtensionFormatLoader's dlopen path
// entirely (see ExtensionRegistry::submit_static_extensions).
int feather_main(int argc, char* argv[], const FeatherExtensionFn* statics = nullptr, size_t statics_count = 0);

} // namespace feather
