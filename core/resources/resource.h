#pragma once

#include "rid.h"
#include <core/framework/reflected.h>
#include <core/framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "resource.gen.h"
#endif

namespace feather {

class Resource : public Reflected {
	FCLASS();
	friend class ResourceLoader;

	RID _rid;

	[[name(path), get(public), set(public)]]
	Path _cached_path;

protected:
	Resource() = default;

public:
	// Resources may give specific ids
	[[method]]
	virtual RID get_rid() { return _rid; };

	virtual bool is_loaded() { return false; };
};

} // namespace feather