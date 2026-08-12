#pragma once
#include "resource.h"
#include "extension_abi.h"

#include <framework/callable.h>
#include <framework/static_string.hpp>

#ifndef FEATHER_REFLECTION_PARSER
#include "extension.gen.h"
#endif

namespace feather {

class SharedLibrary;
class ExtensionFormatLoader;
class ExtensionRegistry;

class FEATHER_API Extension final : public Resource {
	FCLASS();

	friend class ExtensionFormatLoader;
	friend class ExtensionRegistry;

	std::string _name;
	int32_t _priority = FEATHER_EXT_PRIORITY_DEFAULT;
	void (*_initialize)(FeatherExtensionContext*) = nullptr;
	void (*_deinitialize)(FeatherExtensionContext*) = nullptr;

	// Deprecated _load_extension() fallback only (see ExtensionFormatLoader):
	// a legacy plugin has no initialize/deinitialize function pointers at
	// all, just one entry point looked up by name at activation time.
	// ExtensionRegistry resolves and calls this instead of _initialize when
	// _initialize is null.
	std::string _legacy_entry_point;

	std::shared_ptr<SharedLibrary> _library_handle;

	// Filled in by ExtensionRegistry::activate_all() right after this
	// extension's initialize()/_legacy_entry_point runs, via a ClassDB
	// before/after diff -- see extension_registry.cpp. shutdown_all() reads
	// this to know exactly which ClassDB entries to remove before dropping
	// _library_handle, which is what fixes the plugin-resident
	// std::function-destructor crash documented on Main's member list
	// (feather_main.cpp).
	std::vector<StaticString> _owned_class_names;

public:
	Extension() = default;
	~Extension() override = default;

	// Deprecated: exists only so the _load_extension() fallback (and,
	// transitively, any plugin still built against that ABI) keeps
	// compiling for one release. Remove alongside that fallback once every
	// in-tree consumer has migrated to FEATHER_DEFINE_EXTENSION.
	Extension(const std::string_view& name, const std::string_view& entry_point);

	bool is_loaded() override { return _library_handle != nullptr; }

	const std::string& get_name() const { return _name; }
};

} // namespace feather
