#pragma once

#include "resource_format_loader.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "extension_format_loader.gen.h"
#endif

namespace feather {

class FEATHER_API ExtensionFormatLoader : public ResourceFormatLoader {
	FCLASS();

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;
	bool requires_immediate_load() const override { return true; }

public:
	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
