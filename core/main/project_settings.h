#pragma once

#include <framework/export_defs.h>
#include <framework/path.h>
#include <framework/reflected.h>
#include <framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "project_settings.gen.h"
#endif

namespace feather {

class FEATHER_API ProjectSettings final : public Reflected {
	FCLASS(singleton);

	friend class Engine;

	[[get(public), set(public)]]
	Path _project_path;
	std::string _project_name;

public:
	ProjectSettings();
	bool init();

	std::string get_project_name() const;
	Path localize_path(const Path& path);
};

} //namespace feather
