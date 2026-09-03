#pragma once

#include "resource_format_loader.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "fext_format_loader.gen.h"
#endif

namespace feather {

// Loads .fext manifests: a small JSON file declaring an extension, rather than
// a shared library the engine has to open to find out whether it is one.
//
// The only way a project ships a native extension. A plugin in any language --
// C++ included -- reaches the engine through the generated C bindings, and so
// has no C++ Extension object to hand back. A manifest names the entry point
// instead, so the engine constructs the Extension itself and the plugin exports
// nothing but a plain C function.
class FextFormatLoader : public ResourceFormatLoader {
	FCLASS();

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;
	bool requires_immediate_load() const override { return true; }

public:
	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
