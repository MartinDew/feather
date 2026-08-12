#include "extension_registry.h"
#include "extension.h"
#include "resource_loader.h"

#include <framework/shared_library.h>
#include <main/class_db.h>

#include <algorithm>
#include <iostream>
#include <iterator>

namespace feather {

FSINGLETON_INSTANCE(ExtensionRegistry)

ExtensionRegistry::ExtensionRegistry() {
	FSINGLETON_CONSTRUCT_INSTANCE()
}

void ExtensionRegistry::submit(std::shared_ptr<Extension> extension) {
	_pending.push_back(std::move(extension));
}

void ExtensionRegistry::submit_static_extensions(const FeatherExtensionFn* fns, size_t count) {
	for (size_t i = 0; i < count; ++i) {
		const FeatherExtensionDesc* desc = fns[i]();
		if (!desc) {
			std::cerr << "ExtensionRegistry: static extension #" << i << " returned a null descriptor" << std::endl;
			continue;
		}

		auto ext = std::make_shared<Extension>();
		ext->_name = desc->name ? desc->name : "";
		ext->_priority = desc->priority;
		ext->_initialize = desc->initialize;
		ext->_deinitialize = desc->deinitialize;
		submit(ext);
	}
}

void ExtensionRegistry::activate_all() {
	if (_pending.empty())
		return;

	std::stable_sort(_pending.begin(), _pending.end(), [](const auto& a, const auto& b) {
		if (a->_priority != b->_priority)
			return a->_priority < b->_priority;
		return a->_name < b->_name;
	});

	for (auto& ext : _pending) {
		// _class_infos is a std::map, so both snapshots come back sorted by
		// the same key order -- std::set_difference below is valid without
		// re-sorting either side.
		auto before = ClassDB::get_all_class_names();

		if (ext->_initialize) {
			ext->_initialize(nullptr);
		}
		else if (!ext->_legacy_entry_point.empty()) {
			// Deprecated _load_extension() ABI -- see ExtensionFormatLoader.
			Callable entry_fn = ext->_library_handle->get_symbol(ext->_legacy_entry_point);
			if (entry_fn.is_valid()) {
				entry_fn.call();
			}
			else {
				std::cerr << "ExtensionRegistry: legacy entry point '" << ext->_legacy_entry_point
						  << "' not found in extension '" << ext->get_name() << "'" << std::endl;
			}
		}

		auto after = ClassDB::get_all_class_names();
		ext->_owned_class_names.clear();
		std::set_difference(after.begin(), after.end(), before.begin(), before.end(),
				std::back_inserter(ext->_owned_class_names));

		_active.push_back(ext);
	}
	_pending.clear();
}

void ExtensionRegistry::shutdown_all() {
	// Reverse activation order: undo in the opposite order things were
	// brought up, same reasoning a destructor's member teardown follows.
	for (auto it = _active.rbegin(); it != _active.rend(); ++it) {
		if ((*it)->_deinitialize) {
			(*it)->_deinitialize(nullptr);
		}
	}

	// Every extension's classes must be gone from ClassDB before ANY
	// SharedLibrary handle drops -- ClassInfo::object_create_func and
	// Method::callable are std::function objects whose captured closures
	// are code compiled into the plugin (see ClassDB::unregister_class's
	// comment). This has to fully finish before the loop below even starts;
	// it does NOT need to interleave per-extension with anything else here.
	for (auto& ext : _active) {
		for (auto& name : ext->_owned_class_names) {
			ClassDB::unregister_class(name.str());
		}
	}

	// Likewise: cached Resource instances and format loader instances may
	// have vtables living in a plugin, or (the Extension resource itself)
	// may directly own a plugin's SharedLibrary handle. Must be cleared
	// before that handle's refcount is allowed to reach zero below.
	ResourceLoader::get()->clear_caches();

	// Only now may the last shared_ptr references actually go away, taking
	// each Extension's SharedLibrary handle (and thus the loaded module)
	// down with it.
	_active.clear();
	_pending.clear();
}

} // namespace feather
