#pragma once
#define ARGS_NOEXCEPT
#include "launch_settings.h"

#include <args.hxx>
#include <filesystem>

namespace feather {

class LaunchSettings {
	args::ArgumentParser parser{ "Feather Engine" };

	static LaunchSettings* instance;

	args::HelpFlag help{ parser, "help", "display this help menu", { 'h', "help" } };

public:
	LaunchSettings(int argc, char* argv[]);

	args::Positional<std::filesystem::path> project_path{parser, "project path", "the path to the project directory", std::filesystem::current_path().c_str()};
	
	// will be more complex eventually but for now just a flag for windowed vs dummy
	args::ValueFlag<std::string> windowed{ parser, "window mode", "the window mode to use (windowed {default} | headless )", { "w" }, "windowed" };

	static LaunchSettings& get() { return *instance; }
};

} //namespace feather