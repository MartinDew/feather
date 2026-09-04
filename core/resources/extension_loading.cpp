#include "extension_loading.h"

#include "extension.h"

#include <framework/shared_library.h>
#include <main/init_level.h>

#include <iostream>
#include <print>

namespace feather {

void resolve_and_register_extension_entry(const std::shared_ptr<Extension>& ext,
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name) {
	// Resolved as a typed pointer rather than through Callable: the entry point takes an InitLevel, and Callable only carries
	// the signature it was built from -- SharedLibrary::get_symbol() builds one for `void()`.
	auto entry_fn = lib->get_typed_symbol<ExtensionEntryFn>(ext->get_entry_point());
	if (!entry_fn) {
		std::cerr << loader_name << ": Entry point '" << ext->get_entry_point()
				  << "' not found in extension: " << path << std::endl;
		return;
	}

	// Calls the entry point once per level already entered, and leaves it
	// registered for the levels still to come.
	register_extension_entry(ext, entry_fn);

	std::println(std::cout, "{}: Loaded extension '{}' from {}", loader_name, ext->get_name(), path.string());
}

} // namespace feather
