#pragma once

#include "resource.h"
#include "resource_format_loader.h"
#include "rid.h"

#include <framework/reflected.h>
#include <framework/reflection_macros.h>

#include <atomic>

#ifndef FEATHER_REFLECTION_PARSER
#include "resource_loader.gen.h"
#endif

namespace feather {

class FEATHER_API ResourceLoader : public Reflected {
	FCLASS(singleton);

	std::atomic<size_t> m_counter { 1 };

	std::unordered_map<RID, std::shared_ptr<Resource>> _cache;
	std::unordered_map<std::string, std::shared_ptr<Resource>> _path_cache;
	std::vector<std::shared_ptr<ResourceFormatLoader>> _format_loaders;

public:
	ResourceLoader();

	static RID generate_rid();

	// Allows manually registering a resource
	// Useful for resources that are static and don't need to be loaded
	static void register_resource(std::shared_ptr<Resource> res);

	std::shared_ptr<Resource> load(const Path& path);
	template <std::derived_from<Resource> T>
	std::shared_ptr<T> load(const Path& path) {
		auto ptr = load(path);
		return std::static_pointer_cast<T>(ptr);
	}

	void add_resource_format_loader(std::shared_ptr<ResourceFormatLoader> loader);
	void remove_resource_format_loader(std::shared_ptr<ResourceFormatLoader> loader);

	void index_project();

	// Drops every cached Resource and every format loader (engine's own
	// built-ins included, not just a plugin's). Called once, by
	// ExtensionRegistry::shutdown_all(), before any plugin's SharedLibrary
	// handle is dropped: _cache/_path_cache may hold Resource instances
	// whose vtable lives in a plugin (or the Extension resource itself,
	// which OWNS that plugin's handle), and _format_loaders may hold
	// ResourceFormatLoader instances a plugin registered (e.g.
	// GameSettingsFormatLoader) -- all of it must be gone before the module
	// backing that code is unmapped. Only meaningful during final shutdown;
	// nothing after this call is expected to load resources again.
	void clear_caches();
};

} //namespace feather