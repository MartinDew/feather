#pragma once

#include "callable.h"

#include <memory>
#include <string>

// Forward-declared rather than including <SDL3/SDL_loadso.h>: this header is
// on every plugin's include path via feather_public_api, and SDL should not
// be part of the plugin ABI (see get_typed_symbol below for how the template
// avoids needing the complete type).
struct SDL_SharedObject;

namespace feather {

class SharedLibrary {
	SDL_SharedObject* _handle;

public:
	SharedLibrary();
	~SharedLibrary();

	bool load(const std::string& path);
	void unload();

	// Reason the last load()/get_symbol() failed, straight from the platform
	// loader (SDL_GetError wraps dlerror/GetLastError). Worth printing: an
	// unresolved symbol in a project DLL fails the whole dlopen(), and without
	// this the caller only knows *that* it failed, not which symbol was missing.
	[[nodiscard]] static std::string get_last_error();

	[[nodiscard]] Callable get_symbol(const std::string& name) const;
	[[nodiscard]] bool is_loaded() const;

private:
	// Out-of-line so this TU (and every TU that includes this header, i.e.
	// every plugin) never needs <SDL3/SDL_loadso.h>. Defined in the .cpp,
	// where SDL_LoadFunction is actually called.
	[[nodiscard]] void* _raw_symbol(const std::string& name) const;

public:
	template <typename Fn>
	[[nodiscard]] Fn get_typed_symbol(const std::string& name) const {
		return reinterpret_cast<Fn>(_raw_symbol(name));
	}
};

} // namespace feather
