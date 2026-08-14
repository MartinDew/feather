#pragma once
#include "resource.h"

#include <extension/feather_interface.h>

#ifndef FEATHER_REFLECTION_PARSER
#include "extension.gen.h"
#endif

namespace feather {

class SharedLibrary;
class ExtensionFormatLoader;

class Extension final : public Resource {
	FCLASS();

	friend class ExtensionFormatLoader;

	std::string _extension_name;
	std::shared_ptr<SharedLibrary> _library_handle;
	FeatherInitialization _c_abi_init {};

public:
	Extension() = default;
	~Extension() override = default;

	bool is_loaded() override { return _library_handle != nullptr; }

	const std::string& get_name() const { return _extension_name; }
};

} // namespace feather