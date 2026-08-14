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
	std::string _entry_point;
	std::shared_ptr<SharedLibrary> _library_handle;

	// Set instead of _entry_point when the plugin exports feather_extension_init
	// (the C ABI) rather than the legacy _load_extension()/named-symbol pair.
	bool _is_c_abi = false;
	FeatherInitialization _c_abi_init {};

public:
	Extension() = default;
	~Extension() override = default;

	Extension(const std::string_view& name, const std::string_view& entry_point);

	bool is_loaded() override { return _library_handle != nullptr; }

	const std::string& get_name() const { return _extension_name; }
	const std::string& get_entry_point() const { return _entry_point; }
};

} // namespace feather