#pragma once

#include "resource_format_loader.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "material_format_loader.gen.h"
#endif

namespace feather {

class FEATHER_API MaterialFormatLoader : public ResourceFormatLoader {
	FCLASS();

protected:
	std::shared_ptr<Resource> instantiate(const Path& path) override;
	void load(std::shared_ptr<Resource> resource, const Path& path) override;

public:
	bool recognize_extension(const std::string& extension) const override;
};

} // namespace feather
