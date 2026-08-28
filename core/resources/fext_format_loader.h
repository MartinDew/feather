#pragma once

#include "resource_format_loader.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "fext_format_loader.gen.h"
#endif

namespace feather {

// Loads .fext manifests: a small JSON file declaring an extension, rather than
// a shared library the engine has to open to find out whether it is one.
//
// This is what lets a project ship extensions written in languages other than
// C++. A C or C# plugin talks to the engine through the generated C bindings
// (libfeather_c), and has no reasonable way to hand back a C++ Extension
// object -- which is what the _load_extension export exists to do. A manifest
// names the entry point instead, so the engine can construct the Extension
// itself and the plugin only has to export a plain C function.
//
// ExtensionFormatLoader still probes shared libraries for _load_extension, so
// C++ extensions that predate manifests keep loading exactly as before.
class FEATHER_API FextFormatLoader : public ResourceFormatLoader {
	FCLASS();

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;
	bool requires_immediate_load() const override { return true; }

public:
	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
