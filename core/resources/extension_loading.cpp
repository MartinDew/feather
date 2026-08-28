#include "extension_loading.h"

#include "extension.h"

#include <framework/shared_library.h>
#include <main/init_level.h>

#include <iostream>
#include <print>

namespace feather {

std::shared_ptr<Extension> probe_legacy_extension(
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name) {
	using LoadExtensionFn = Extension* (*)();
	auto load_fn = lib->get_typed_symbol<LoadExtensionFn>("_load_extension");
	if (!load_fn) {
		// Not a feather extension — silently ignore
		return nullptr;
	}

	// The extension is `new`'d in the DLL's own CRT heap, so it must be
	// `delete`d there too -- static CRT linking gives each binary its own
	// private heap on Windows, and freeing across that boundary corrupts it.
	using DestroyExtensionFn = void (*)(Extension*);
	auto destroy_fn = lib->get_typed_symbol<DestroyExtensionFn>("_destroy_extension");
	if (!destroy_fn) {
		std::cerr << loader_name << ": extension exports _load_extension but not _destroy_extension: " << path
				  << std::endl;
		return nullptr;
	}

	Extension* ext_raw = load_fn();
	if (!ext_raw) {
		return nullptr;
	}

	// Capture `lib` in the deleter so the library stays mapped for the full
	// duration of destroy_fn's call -- ext_raw's own destructor drops its
	// _library_handle reference as part of running inside destroy_fn.
	return std::shared_ptr<Extension>(ext_raw, [destroy_fn, lib](Extension* p) { destroy_fn(p); });
}

void resolve_and_register_extension_entry(const std::shared_ptr<Extension>& ext,
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name) {
	// Resolved as a typed pointer rather than through Callable: the entry
	// point takes an InitLevel, and Callable only carries the signature it was
	// built from -- SharedLibrary::get_symbol() builds one for `void()`.
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
