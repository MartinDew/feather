#pragma once

#include "callable.h"

#include <SDL3/SDL_loadso.h>
#include <memory>
#include <string>

namespace feather {

class SharedLibrary {
	SDL_SharedObject* _handle;

public:
	SharedLibrary();
	~SharedLibrary();

	bool load(const std::string& path);
	void unload();

	[[nodiscard]] Callable get_symbol(const std::string& name) const;
	[[nodiscard]] bool is_loaded() const;

	template <typename Fn>
	[[nodiscard]] Fn get_typed_symbol(const std::string& name) const {
		if (!_handle)
			return nullptr;
		auto sym = SDL_LoadFunction(_handle, name.c_str());
		return reinterpret_cast<Fn>(sym);
	}
};

} // namespace feather
