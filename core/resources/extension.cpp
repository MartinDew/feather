#include "extension.h"

namespace feather {

Extension::Extension(const std::string_view& name, const std::string_view& entry_point)
		: _extension_name(name)
		, _entry_point(entry_point) {
}

} // namespace feather
