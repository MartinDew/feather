#pragma once

#include <core/framework/path.h>

namespace feather {

// Runs `script` in an embedded CPython interpreter, starting one on first use.
// Returns false and reports to stderr if the interpreter or the script fails.
bool py_host_run_script(const Path& script);

// Shuts the interpreter down if one was started.
void py_host_finalize();

// Anchors py_ecs.cpp into the link.
//
// Everything in that file is reached through a static initializer -- pybind11's
// embedded-module registration, which has to run before Py_Initialize -- so no
// symbol in it is referenced by name. This module is a static library, and a
// library member nothing references is not linked at all, which would leave the
// interpreter with no _feather_ecs to import. Calling this is what keeps it.
void py_ecs_link_anchor();

} // namespace feather
