#pragma once

#include "rid.h"
#include <core/framework/reflected.h>
#include <core/framework/reflection_macros.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "resource.gen.h"
#endif

namespace feather {

class FEATHER_API Resource : public Reflected {
	FCLASS();
	friend class ResourceLoader;

	RID _rid;

	[[name(path), get(public, ref), set(public, ref)]]
	Path _cached_path;

	[[get(public), set(protected)]]
	bool _should_be_loaded;

protected:
	Resource() = default;

public:
	// Resources may give specific ids
	[[method]]
	virtual RID get_rid() {
		return _rid;
	};

	virtual bool is_loaded() { return false; };
};

} // namespace feather