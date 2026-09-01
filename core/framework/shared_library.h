#pragma once

#include "callable.h"
#include "export_defs.h"

#include <memory>
#include <string>

namespace feather {

class FEATHER_API SharedLibrary {
	void* _handle;

public:
	SharedLibrary();
	~SharedLibrary();

	bool load(const std::string& path);
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
