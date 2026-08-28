#pragma once

#include <core/framework/path.h>

namespace feather {

// Runs `script` in an embedded CPython interpreter, starting one on first use.
// Returns false and reports to stderr if the interpreter or the script fails.
bool py_host_run_script(const Path& script);

// Shuts the interpreter down if one was started.
void py_host_finalize();

} // namespace feather
