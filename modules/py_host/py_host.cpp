#include "py_host.h"

#include <SDL3/SDL_filesystem.h>

#include <Python.h>

#include <filesystem>
#include <iostream>
#include <print>

namespace feather {

namespace {

bool _initialized = false;

// Puts the shipped `feather` module on the import path.
//
// The module is built to bind against the engine process rather than link the
// engine (see modules/py_bindings/xmake.lua), so it only works inside this
// interpreter -- which is why it ships next to the engine binary rather than
// being installed for the system Python.
void _add_module_path() {
	const char* base_path = SDL_GetBasePath();
	if (!base_path) {
		return;
	}

	auto module_dir = (std::filesystem::path(base_path) / "python").string();

	PyObject* sys_path = PySys_GetObject("path"); // borrowed
	if (!sys_path) {
		return;
	}

	PyObject* entry = PyUnicode_FromString(module_dir.c_str());
	if (!entry) {
		return;
	}
	// Prepended: a `feather` module elsewhere on the path would otherwise
	// shadow the one that can actually talk to this process.
	PyList_Insert(sys_path, 0, entry);
	Py_DECREF(entry);
}

bool _ensure_initialized() {
	if (_initialized) {
		return true;
	}

	// 0: don't install signal handlers. The engine owns SIGINT -- an
	// interpreter that grabbed it would swallow Ctrl-C.
	Py_InitializeEx(0);
	if (!Py_IsInitialized()) {
		std::cerr << "py_host: failed to initialize the Python interpreter" << std::endl;
		return false;
	}

	_add_module_path();
	_initialized = true;
	return true;
}

} // namespace

bool py_host_run_script(const Path& script) {
	if (!_ensure_initialized()) {
		return false;
	}

	FILE* file = std::fopen(script.string().c_str(), "r");
	if (!file) {
		std::cerr << "py_host: cannot open script: " << script << std::endl;
		return false;
	}

	// The last argument closes the file for us.
	int result = PyRun_SimpleFileEx(file, script.string().c_str(), 1);
	if (result != 0) {
		// PyRun_SimpleFileEx has already printed the traceback.
		std::cerr << "py_host: script raised an exception: " << script << std::endl;
		return false;
	}

	return true;
}

void py_host_finalize() {
	if (!_initialized) {
		return;
	}

	// Failures here are reported but not fatal: the engine is shutting down,
	// and a script's stray reference cycle must not take it with them.
	if (Py_FinalizeEx() < 0) {
		std::cerr << "py_host: the Python interpreter did not shut down cleanly" << std::endl;
	}
	_initialized = false;
}

} // namespace feather
