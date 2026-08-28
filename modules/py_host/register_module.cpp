#include "register_module.h"

#include "py_host.h"

#include <core/resources/script_extension_runner.h>

namespace feather {

void register_py_host(InitLevel level) {
	// Core: the runner has to be in place before ResourceLoader::index_project()
	// reaches a .fext manifest of type "python", which happens right after the
	// Servers level is entered.
	if (level != InitLevel::Core) {
		return;
	}

	register_script_extension_runner("python", [](const Path& script) { return py_host_run_script(script); });
}

void unregister_py_host(InitLevel level) {
	if (level != InitLevel::Core) {
		return;
	}

	py_host_finalize();
}

} //namespace feather
