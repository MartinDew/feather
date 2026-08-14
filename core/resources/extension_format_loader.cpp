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

	auto init_fn = lib->get_typed_symbol<FeatherInitializationFn>("feather_extension_init");
	if (!init_fn) {
		// Not a feather extension — silently ignore
		return nullptr;
	}

	FeatherInitialization init {};
	FeatherLibraryPtr library_token = lib.get();
	if (!init_fn(&feather_get_proc_address, library_token, &init)) {
		std::cerr << "ExtensionFormatLoader: feather_extension_init failed: " << path << std::endl;
		return nullptr;
	}

	auto ext = std::make_shared<Extension>();
	ext->_extension_name = path.stem().string();
	ext->_c_abi_init = init;
	ext->_library_handle = lib;
	ext->set_path(path);
	return ext;
}

void ExtensionFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	auto ext = std::static_pointer_cast<Extension>(resource);
	ExtensionRegistry::register_extension(ext->_c_abi_init);
	std::println(std::cout, "ExtensionFormatLoader: Loaded extension '{}' from {}", ext->get_name(), path.string());
}

} // namespace feather
