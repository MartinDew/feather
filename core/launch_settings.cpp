#include "launch_settings.h"

namespace feather {

LaunchSettings* LaunchSettings::instance = nullptr;

LaunchSettings::LaunchSettings(int argc, char* argv[]) {
	if (!instance)
		throw std::runtime_error("LaunchSettings instance already exists!");

	instance = this;

	parser.ParseCLI(argc, argv);
}

} //namespace feather