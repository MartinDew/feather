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

	SDL_UnloadObject(static_cast<SDL_SharedObject*>(_handle));
	_handle = nullptr;
}

void* SharedLibrary::resolve_symbol(const std::string& name) const {
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
	return _handle != nullptr;
}

} // namespace feather
