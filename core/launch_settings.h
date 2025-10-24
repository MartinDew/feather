#pragma once
#define ARGS_NOEXCEPT
#include <args.hxx>

namespace feather {

class LaunchSettings {
	args::ArgumentParser parser{ "Feather Engine" };

	static LaunchSettings* instance;

	args::HelpFlag help{ parser, "help", "display this help menu", { 'h', "help" } };

public:
	LaunchSettings(int argc, char* argv[]);

	// will be more complex eventually but for now just a flag for windowed vs dummy
	args::ValueFlag<std::string> windowed{ parser, "window mode", "the window mode to use (windowed {default} | headless )", { "w" }, "windowed" };

	static LaunchSettings& get() { return *instance; }
};

} //namespace feather