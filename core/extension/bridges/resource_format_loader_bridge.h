#pragma once

#include <extension/feather_interface.h>
#include <resources/resource_format_loader.h>

namespace feather {

void register_resource_format_loader_bridge();

// Engine-side shim for a plugin-registered ResourceFormatLoader subclass.
// One instance per plugin class registration (see ClassDB::register_extension_class
// in feather_interface.cpp) -- the engine allocates and owns this, never the
// plugin; only _plugin_instance (the plugin's own opaque state) crosses back
// and forth through the cached virtual function pointers.
class ExtensionResourceFormatLoader final : public ResourceFormatLoader {
	FeatherExtensionClassInfo _info;
	void* _plugin_instance = nullptr;

	FeatherProc _recognize_extension_fn = nullptr;
	FeatherProc _instantiate_fn = nullptr;
	FeatherProc _load_fn = nullptr;
	FeatherProc _requires_immediate_load_fn = nullptr;

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;
	bool requires_immediate_load() const override;

public:
	explicit ExtensionResourceFormatLoader(const FeatherExtensionClassInfo& info);
	~ExtensionResourceFormatLoader() override;

	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
