#include "extension_format_loader.h"
#include "extension.h"
#include <extension/extension_interface.h>
#include <extension/extension_registry.h>
#include <framework/shared_library.h>
#include <iostream>

namespace feather {

bool ExtensionFormatLoader::recognize_extension(const std::string& extension) const {
	return extension == "dll" || extension == "so" || extension == "dylib";
}

std::shared_ptr<Resource> ExtensionFormatLoader::instantiate(const Path& path) {
	auto lib = std::make_shared<SharedLibrary>();
	if (!lib->load(path.string())) {
		std::cerr << "ExtensionFormatLoader: Failed to load library: " << path << ": "
				  << SharedLibrary::get_last_error() << std::endl;
		return nullptr;
	}

	// C ABI path (docs/plugin_abi.md), tried first. The legacy _load_extension()
	// path below stays alive until every consumer has migrated.
	if (auto init_fn = lib->get_typed_symbol<FeatherInitializationFn>("feather_extension_init")) {
		FeatherInitialization init {};
		FeatherLibraryPtr library_token = lib.get();
		if (!init_fn(&feather_get_proc_address, library_token, &init)) {
			std::cerr << "ExtensionFormatLoader: feather_extension_init failed: " << path << std::endl;
			return nullptr;
		}

		auto ext = std::make_shared<Extension>();
		ext->_extension_name = path.stem().string();
		ext->_is_c_abi = true;
		ext->_c_abi_init = init;
		ext->_library_handle = lib;
		ext->set_path(path);
		return ext;
	}

	using LoadExtensionFn = Extension* (*)();
	auto load_fn = lib->get_typed_symbol<LoadExtensionFn>("_load_extension");
	if (!load_fn) {
		// Not a feather extension — silently ignore
		return nullptr;
	}

	Extension* ext_raw = load_fn();
	if (!ext_raw) {
		return nullptr;
	}

	std::shared_ptr<Extension> ext(ext_raw);
	ext->_library_handle = lib;
	ext->set_path(path);
	return ext;
}

void ExtensionFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	auto ext = std::static_pointer_cast<Extension>(resource);

	if (ext->_is_c_abi) {
		ExtensionRegistry::register_extension(ext->_c_abi_init);
		std::println(std::cout, "ExtensionFormatLoader: Loaded extension '{}' from {}", ext->get_name(), path.string());
		return;
	}

	Callable entry_fn = ext->_library_handle->get_symbol(ext->get_entry_point());
	if (entry_fn.is_valid()) {
		entry_fn.call();
		std::println(std::cout, "ExtensionFormatLoader: Loaded extension '{}' from {}", ext->get_name(), path.string());
	}
	else {
		std::cerr << "ExtensionFormatLoader: Entry point '" << ext->get_entry_point()
				  << "' not found in extension: " << path << std::endl;
	}
}

} // namespace feather
