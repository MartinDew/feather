#pragma once
#include "resource.h"
#include <framework/export_defs.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "script.gen.h"
#endif

namespace feather {

class ScriptFormatLoader;

// A script file the engine ran.
//
// Distinct from Extension on purpose: an extension is a library with an entry
// point the engine calls back into per init level, while a script is source
// that runs once, when the project is indexed. What it leaves behind -- the
// components and systems it registered -- lives in the world and in ClassDB,
// not here.
//
// Its presence is what marks the file as indexed, so it is not picked up twice.
class FEATHER_API Script final : public Resource {
	FCLASS();

	friend class ScriptFormatLoader;

	std::string _language;
	bool _ran = false;

public:
	Script() = default;
	~Script() override = default;

	explicit Script(std::string language) : _language(std::move(language)) {}

	bool is_loaded() override { return _ran; }

	// The runner that was used ("python"), not the file extension.
	const std::string& get_language() const { return _language; }
};

} // namespace feather
