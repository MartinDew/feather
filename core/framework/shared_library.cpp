#include "shared_library.h"

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

bool SharedLibrary::is_loaded() const {
	return _handle != nullptr;
}

} // namespace feather
