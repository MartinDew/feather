#pragma once
#define ARGS_NOEXCEPT

#include <args.hxx>
#include <filesystem>

namespace feather {

class LaunchSettings {
	args::ArgumentParser _parser{ "Feather Engine" };

	static LaunchSettings* instance;

public:
	LaunchSettings(int argc, char* argv[]);

	args::Positional<std::filesystem::path> project_path{ _parser, "project path", "the path to the project directory", std::filesystem::current_path().c_str() };

#ifdef EDITOR_MODE
	// Editor mode?
	args::ValueFlag<bool> editor_mode{ _parser, "editor", "should run in editor mode", { 'e', "editor" }, true };
#endif

	// will be more complex eventually but for now just a flag for windowed vs dummy
	args::ValueFlag<std::string> windowed{ _parser, "window mode", "the window mode to use (windowed {default} | headless )", { "w" }, "windowed" };

	static LaunchSettings& get() { return *instance; }

private:
	args::HelpFlag _help{ _parser, "help", "display this help menu", { 'h', "help" } };
};

} //namespace feather