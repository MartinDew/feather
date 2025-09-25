#include "settings.h"
#include <cstddef>
#include <string_view>

LaunchSettings* LaunchSettings::instance = nullptr;

const LaunchSettings& LaunchSettings::Get() { return *instance; }

LaunchSettings::LaunchSettings(size_t argc, char* argv[]) {
	for (size_t i = 0; i < argc; ++i) {
		std::string_view view(argv[i]);
		if (view[0] == '-') {
			auto arg_name = view.substr(1);
		}
	}
}