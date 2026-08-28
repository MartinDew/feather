#include "shared_library.h"
#include "callable.h"

#include <SDL3/SDL_loadso.h>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace feather {

SharedLibrary::SharedLibrary() : _handle(nullptr), _global_handle(nullptr) {
}

SharedLibrary::~SharedLibrary() {
	unload();
}

bool SharedLibrary::load(const std::string& path, bool global_symbols) {
	unload();

#ifndef _WIN32
	if (global_symbols) {
		_global_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
		return _global_handle != nullptr;
	}
#else
	// Windows has a single, process-wide symbol scope; nothing to opt into.
	(void)global_symbols;
#endif

	_handle = SDL_LoadObject(path.c_str());

	return _handle != nullptr;
}

std::string SharedLibrary::get_last_error() {
#ifndef _WIN32
	// dlopen() reports through dlerror(), which SDL_GetError() knows nothing
	// about. Only meaningful straight after a failed global load, and harmless
	// otherwise: dlerror() returns null when there's nothing pending.
	if (const char* dl_err = dlerror()) {
		return dl_err;
	}
#endif
	const char* err = SDL_GetError();
	return err ? err : "";
}

void SharedLibrary::unload() {
#ifndef _WIN32
	if (_global_handle) {
		dlclose(_global_handle);
		_global_handle = nullptr;
		return;
	}
#endif

	if (!_handle) {
		return;
	}

	SDL_UnloadObject(static_cast<SDL_SharedObject*>(_handle));
	_handle = nullptr;
}

void* SharedLibrary::resolve_symbol(const std::string& name) const {
#ifndef _WIN32
	if (_global_handle) {
		return dlsym(_global_handle, name.c_str());
	}
#endif

	if (!_handle) {
		return nullptr;
	}

	return reinterpret_cast<void*>(SDL_LoadFunction(static_cast<SDL_SharedObject*>(_handle), name.c_str()));
}

Callable SharedLibrary::get_symbol(const std::string& name) const {
	auto sym = resolve_symbol(name);
	if (!sym) {
		return {};
	}

	void (*sym_cpp)() = reinterpret_cast<void (*)()>(sym);
	return Callable(sym_cpp);
}

bool SharedLibrary::is_loaded() const {
	return _handle != nullptr || _global_handle != nullptr;
}

} // namespace feather
