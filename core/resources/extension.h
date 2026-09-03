#pragma once
#include "resource.h"

#ifndef FEATHER_REFLECTION_PARSER
#include "extension.gen.h"
#endif

namespace feather {

class SharedLibrary;
class FextFormatLoader;

class Extension final : public Resource {
	FCLASS();

	friend class FextFormatLoader;

	std::string _extension_name;
	std::string _entry_point;
	std::shared_ptr<SharedLibrary> _library_handle;

public:
	Extension() = default;
	~Extension() override = default;

	Extension(const std::string_view& name, const std::string_view& entry_point);

	bool is_loaded() override { return _library_handle != nullptr; }

	const std::string& get_name() const { return _extension_name; }
	const std::string& get_entry_point() const { return _entry_point; }
};

} // namespace feather