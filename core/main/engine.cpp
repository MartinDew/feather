#include "engine.h"
#include "launch_settings.h"

#include <framework/assert.h>
#include <resources/resource_loader.h>

#include <chrono>
#include <csignal>

#include <rendering/rendering_server.h>

namespace feather {

namespace {

// Headless has no window, so SDL never delivers SDL_EVENT_WINDOW_CLOSE_REQUESTED
// and the main loop would otherwise have no exit condition at all.
// volatile sig_atomic_t is the only type a signal handler may portably touch.
volatile std::sig_atomic_t g_quit_requested = 0;

void _on_terminate_signal(int) {
	g_quit_requested = 1;
}

} //namespace

Engine* Engine::_instance = nullptr;

Engine::Engine() {
	// Todo replace sdl assert by custom one
	fassert(!_instance);

	_instance = this;
	_rendering_server.init();
}

Engine::~Engine() {
	// unregister_modules();
}

bool Engine::run() {
	auto current_time = start_time;

	bool keep_running = true;

	// initialization
	ResourceLoader::get()->index_project();

	// Debug stuff. Must come after index_project(): that's what dlopens a
	// project's extension DLL and runs its entry point, which is where a
	// project's own reflected types (and format loaders, etc.) register
	// themselves with ClassDB. Dumping before it would silently omit every
	// project-defined type.
	if (LaunchSettings::get().dump_db.Get()) {
		ClassDB::get()->print_db();
		return true;
	}

	_world_sim.init();

	// Installed in every mode, not just headless: Ctrl+C on a terminal-launched
	// windowed session should shut down through the normal teardown too.
	std::signal(SIGINT, &_on_terminate_signal);
	std::signal(SIGTERM, &_on_terminate_signal);

	// update
	double accumulator = 0.0;
	while (keep_running && !g_quit_requested) {
		keep_running = _main_window.update();

		auto new_time = Clock::now();

		double frame_time = std::chrono::duration_cast<std::chrono::duration<double>>(new_time - current_time).count();
		current_time = new_time;

		accumulator += frame_time;
		_current_dt = simulation_time;
		while (accumulator >= simulation_time) {
			accumulator -= simulation_time;
			_world_sim.fixed_update(simulation_time);
		}

		_current_dt = frame_time;
		_world_sim.update(frame_time);

		// Tell the renderer to render here
		_rendering_server.update(frame_time);
	}

	// Join the render thread while every Engine member is still alive. Member
	// destruction order (engine.h) otherwise tears down _world_sim and
	// _main_window before the jthread destructor gets to join.
	_rendering_server.stop();

	return true;
}

double Engine::get_current_delta_time() const {
	return _current_dt;
}

bool Engine::is_editor() {
	return LaunchSettings::get().editor_mode.Get();
}

bool Engine::is_headless() {
	// Cached rather than re-read like is_editor(): this is queried from Window's
	// constructor and from the render path, and the flag is a string. Safe to
	// call before Engine::_instance is set -- it only touches LaunchSettings,
	// which Main constructs first (feather_main.cpp).
	static const bool headless = [] {
		const std::string& mode = LaunchSettings::get().windowed.Get();
		if (mode == "headless")
			return true;

		fassert(mode == "windowed",
				std::format("Unknown window mode '{}' (expected 'windowed' or 'headless')", mode));
		return false;
	}();

	return headless;
}
} //namespace feather