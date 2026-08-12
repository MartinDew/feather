#include "extension_format_loader.h"
#include "extension.h"
#include "extension_fingerprint.h"
#include "extension_registry.h"
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

	// feather_extension_main is the fixed symbol name FEATHER_DEFINE_EXTENSION
	// (tools/SDK/include/feather_extension.h) exports, regardless of the
	// plugin's own identifier -- see that header for why. The descriptor it
	// returns lives in the plugin's own static storage; the engine reads it
	// and allocates the Extension itself below, so nothing is `new`'d in one
	// module and `delete`'d in the other.
	using ExtensionEntryFn = const FeatherExtensionDesc* (*)();
	if (auto entry_fn = lib->get_typed_symbol<ExtensionEntryFn>("feather_extension_main")) {
		const FeatherExtensionDesc* desc = entry_fn();
		if (!desc) {
			std::cerr << "ExtensionFormatLoader: feather_extension_main() returned null: " << path << std::endl;
			return nullptr;
		}
		if (desc->abi_version != FEATHER_EXTENSION_ABI_VERSION) {
			std::cerr << "ExtensionFormatLoader: ABI version mismatch loading " << path << " -- plugin built against "
					  << desc->abi_version << ", engine expects " << FEATHER_EXTENSION_ABI_VERSION << std::endl;
			return nullptr;
		}
		if (desc->engine_version != FEATHER_ENGINE_ABI_VERSION) {
			std::cerr << "ExtensionFormatLoader: engine version mismatch loading " << path
					  << " -- plugin built against " << desc->engine_version << ", engine is "
					  << FEATHER_ENGINE_ABI_VERSION << std::endl;
			return nullptr;
		}
		if (desc->build_fingerprint != compute_build_fingerprint()) {
			// The concrete, previously-observed failure mode this catches:
			// a plugin built with a different CRT/debug-iterator config than
			// the engine (e.g. debug plugin against a release engine, or
			// vice versa) -- see decision 7 and this branch's own CI
			// history for why that corrupts the heap instead of just
			// misbehaving. Rejecting loudly here beats that.
			std::cerr << "ExtensionFormatLoader: build fingerprint mismatch loading " << path
					  << " -- plugin and engine were likely built with different CRT/debug settings" << std::endl;
			return nullptr;
		}

		auto ext = std::make_shared<Extension>();
		ext->_name = desc->name ? desc->name : "";
		ext->_priority = desc->priority;
		ext->_initialize = desc->initialize;
		ext->_deinitialize = desc->deinitialize;
		ext->_library_handle = lib;
		ext->set_path(path);
		return ext;
	}

	// Deprecated fallback for one release: a plugin that only exports the
	// old _load_extension() ABI (no descriptor, no version/fingerprint
	// checks possible). Remove once every in-tree consumer has migrated to
	// FEATHER_DEFINE_EXTENSION.
	using LoadExtensionFn = Extension* (*)();
	auto load_fn = lib->get_typed_symbol<LoadExtensionFn>("_load_extension");
	if (!load_fn) {
		// Not a feather extension at all — silently ignore.
		return nullptr;
	}

	std::cerr << "ExtensionFormatLoader: " << path
			  << " uses the deprecated _load_extension() ABI -- migrate to FEATHER_DEFINE_EXTENSION "
				 "(tools/SDK/include/feather_extension.h)"
			  << std::endl;

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
	// Discovery only: submit() records the extension for later activation
	// (Engine::run() calls ExtensionRegistry::activate_all() once every
	// project resource -- every project DLL included -- has been
	// discovered), rather than running its entry point inline here as
	// directory-walk order happens to reach it. See extension_registry.h.
	auto ext = std::static_pointer_cast<Extension>(resource);
	std::println(std::cout, "ExtensionFormatLoader: Loaded extension '{}' from {}", ext->get_name(), path.string());
	ExtensionRegistry::get()->submit(ext);
}

} // namespace feather
