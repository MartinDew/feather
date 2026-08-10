#include "shared_library.h"
#include "callable.h"

#include <SDL3/SDL_loadso.h>

namespace feather {

SharedLibrary::SharedLibrary() : _handle(nullptr) {
}

SharedLibrary::~SharedLibrary() {
	unload();
}

bool SharedLibrary::load(const std::string& path) {
	unload();

	_handle = SDL_LoadObject(path.c_str());

	return _handle != nullptr;
}

std::string SharedLibrary::get_last_error() {
	const char* err = SDL_GetError();
	return err ? err : "";
}

void SharedLibrary::unload() {
	if (!_handle) {
		return;
	}

	SDL_UnloadObject(_handle);
	_handle = nullptr;
}

Callable SharedLibrary::get_symbol(const std::string& name) const {
	if (!_handle) {
		return {};
	}

	auto sym = SDL_LoadFunction(_handle, name.c_str());
	if (!sym) {
		return {};
	}

	void (*sym_cpp)() = reinterpret_cast<void (*)()>(sym);
	return Callable(sym_cpp);
}

bool SharedLibrary::is_loaded() const {
	return _handle != nullptr;
}

void* SharedLibrary::_raw_symbol(const std::string& name) const {
	if (!_handle) {
		return nullptr;
	}

	// SDL_LoadFunction returns SDL_FunctionPointer (void(*)()), not void* --
	// same function-pointer/object-pointer conversion get_symbol() below
	// already relies on (POSIX guarantees dlsym-style round-tripping works;
	// this codebase assumes POSIX-like platforms elsewhere too).
	return reinterpret_cast<void*>(SDL_LoadFunction(_handle, name.c_str()));
}

} // namespace feather
