#pragma once

#include <core/framework/path.h>

#include <functional>
#include <string>

namespace feather {

// How a .fext manifest of a non-native type gets run.
//
// A manifest can name a script instead of a shared library ("type": "python"),
// but core has no business knowing what a Python interpreter is -- and must not
// link one. So core owns this registry and nothing else: whichever module can
// run a given script type registers a runner for it at InitLevel::Core, and
// FextFormatLoader looks the type up here.
//
// Returns false if the script could not be run; the loader reports it.
using ScriptExtensionRunner = std::function<bool(const Path& script)>;

void register_script_extension_runner(std::string type, ScriptExtensionRunner runner);

// Null when nothing has registered that type -- typically because the module
// providing it wasn't built into this engine.
const ScriptExtensionRunner* find_script_extension_runner(const std::string& type);

} // namespace feather
