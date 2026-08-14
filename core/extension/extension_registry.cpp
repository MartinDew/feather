#include "extension_registry.h"

#include <vector>

namespace feather {

namespace {
std::vector<FeatherInitialization>& extensions() {
	static std::vector<FeatherInitialization> instance;
	return instance;
}
} // namespace

void ExtensionRegistry::register_extension(const FeatherInitialization& init) {
	extensions().push_back(init);
	if (init.initialize) {
		init.initialize(init.userdata, FEATHER_INIT_CORE);
	}
}

void ExtensionRegistry::fire_level(FeatherInitializationLevel level) {
	for (auto& ext : extensions()) {
		if (ext.initialize) {
			ext.initialize(ext.userdata, level);
		}
	}
}

} // namespace feather
