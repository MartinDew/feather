#pragma once

#include "resource_format_loader.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "script_format_loader.gen.h"
#endif

namespace feather {

// Runs project scripts that are recognized by their extension alone, with no
// manifest: a .fpy file is to Python what a .cpp extension DLL is to C++.
//
// Only .fpy, deliberately -- not .py. Claiming every .py in a project would
// mean executing whatever happens to be there, including modules a script
// imports and tooling that was never meant to run inside the engine. The
// dedicated extension is the file saying so. A .py sitting next to a .fpy is
// still importable from it, because the script's own directory is on sys.path.
//
// A .fext manifest of type "python" still works and is still the way to give a
// script a name and a declared type; this is the lighter path, for a project
// that just wants to drop a script in.
class FEATHER_API ScriptFormatLoader : public ResourceFormatLoader {
	FCLASS();

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;
	// Scripts define components and systems, which anything loaded after them
	// may expect to exist -- so they run as they are found, like extensions.
	bool requires_immediate_load() const override { return true; }

public:
	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
