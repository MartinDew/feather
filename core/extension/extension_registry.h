#pragma once

#include "feather_interface.h"

namespace feather {

// Tracks loaded C-ABI extensions so a later initialization level can be
// fired on all of them at once, after CORE already fired individually as
// each one loaded. Not yet an unload-ordered registry (see docs/plugin_abi.md)
// -- deinitialize is never called anywhere yet.
class ExtensionRegistry {
public:
	static void register_extension(const FeatherInitialization& init);
	static void fire_level(FeatherInitializationLevel level);
};

} // namespace feather
