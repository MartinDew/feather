#pragma once

#include "framework/assert.h"
#include "framework/static_string.hpp"

#include <args.hxx>

#include <filesystem>
#include <map>

namespace feather {

class LaunchSettings {
	args::ArgumentParser _parser { "Feather Engine" };

	static LaunchSettings* _instance;

	LaunchSettings();

	std::map<StaticString, args::Group*> _groups { { "root"_ss, &_parser } };

public:
	LaunchSettings(int argc, char* argv[]);
	void init(int argc, char* argv[]);

	args::Positional<std::filesystem::path> project_path { _parser,
														   "project path",
														   "The path to the project directory",
														   std::filesystem::current_path().c_str() };

#ifdef EDITOR_BUILD
	// Editor mode?
	args::ImplicitValueFlag<bool> editor_mode { _parser,		   "editor", "Should run in editor mode",
												{ 'e', "editor" }, true,	 false };

	args::ImplicitValueFlag<bool> demo_mode {
		_parser, "demo", "Should run in demo mode (skips loading project but prevents saving)", { "demo" }, true, false
	};
#endif

	// will be more complex eventually but for now just a flag for windowed vs dummy
	args::ValueFlag<std::string> windowed { _parser,
											"window mode",
											"The window mode to use (windowed {default} | headless )",
											{ 'w', "window-mode" },
											"windowed" };

	args::Group rendering { _parser, "Rendering related settings" };

	args::ValueFlag<std::string> renderer;
	args::ImplicitValueFlag<bool> force_single_thread {
		_parser, "single thread", "Force single threaded rendering", { "single-thread" }, true, false
	};

#ifdef EDITOR_BUILD
	args::ImplicitValueFlag<bool> dump_db {
		_parser, "dump db", "dumps the class database", { "dump-db" }, true, false
	};
#endif

	// A headless test hook: exits cleanly, through the same path as SIGINT,
	// after this many real (not fixed) update frames -- i.e. after
	// WorldSim::update() has called world.progress() this many times, which is
	// what actually runs ECS systems. --dump-db exits before World even enters
	// its init level, so nothing that runs in the frame loop -- a system's
	// side effects among them -- can be observed through it; this is the flag
	// for that. 0 (the default) runs indefinitely, unaffected.
	args::ValueFlag<int> run_frames {
		_parser, "frames", "Exit headless after this many real update frames (0 = run indefinitely)",
		{ "run-frames" }, 0
	};

	static LaunchSettings& get();

	static constexpr args::Group& get_group(StaticString name = "root"_ss) {
		if (name == "root"_ss) {
			return get()._parser;
		}

		fassert(get()._groups.contains(name), std::format("No such group {}", name));
		return *get()._groups.at(name);
	}

private:
	args::HelpFlag _help { _parser, "help", "Display this help menu", { 'h', "help" } };
};

} //namespace feather