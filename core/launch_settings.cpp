#include "launch_settings.h"

namespace feather {

LaunchSettings* LaunchSettings::instance = nullptr;

LaunchSettings::LaunchSettings(int argc, char* argv[]) {
	if (instance)
		throw std::runtime_error("LaunchSettings instance already exists!");

	instance = this;

	_parser.ParseCLI(argc, argv);

	switch (_parser.GetError()) {
	case args::Error::Help:
		std ::cout << _parser;
		exit(0);
		break;
	default:
		break;
	}
}

} //namespace feather