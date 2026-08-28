#pragma once

#include "callable.h"
#include "export_defs.h"

#include <memory>
#include <string>

namespace feather {

class FEATHER_API SharedLibrary {
	// Set when the library was loaded through SDL (the default path).
	void* _handle;
	// Set instead of _handle when loaded with global_symbols on POSIX, where
	// the library has to come from a raw dlopen -- see load().
	void* _global_handle;

public:
	SharedLibrary();
	~SharedLibrary();

	// global_symbols publishes the library's symbols to the process-wide
	// lookup scope, so libraries loaded afterwards can bind against them.
	// SDL_LoadObject() is RTLD_LOCAL on POSIX and offers no way to change
	// that, so this takes a raw dlopen() instead; on Windows the distinction
	// doesn't exist and both paths behave identically.
	bool load(const std::string& path, bool global_symbols = false);
	void unload();

	[[nodiscard]] static std::string get_last_error();

	[[nodiscard]] Callable get_symbol(const std::string& name) const;
	[[nodiscard]] bool is_loaded() const;

	template <typename Fn>
	[[nodiscard]] Fn get_typed_symbol(const std::string& name) const {
		return reinterpret_cast<Fn>(resolve_symbol(name));
	}

private:
	// Out of line so the dlfcn.h/SDL split stays in the .cpp.
	[[nodiscard]] void* resolve_symbol(const std::string& name) const;
};

} // namespace feather
