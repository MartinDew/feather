#include "extension_format_loader.h"
#include "extension.h"
#include "extension_loading.h"
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

	auto ext = probe_legacy_extension(lib, path, "ExtensionFormatLoader");
	if (!ext) {
		return nullptr;
	}

	ext->_library_handle = lib;
	ext->set_path(path);
	return ext;
}

void ExtensionFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	auto ext = std::static_pointer_cast<Extension>(resource);
	resolve_and_register_extension_entry(ext, ext->_library_handle, path, "ExtensionFormatLoader");
}

} // namespace feather
