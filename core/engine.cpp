#include "engine.h"
#include "SDL3/SDL_init.h"
#include "launch_settings.h"
#include <chrono>

namespace feather {

namespace {

constexpr double simulation_time = 1.0 / 60.0;

} //namespace

Engine* Engine::_instance = nullptr;

Engine::Engine() {
	// Todo replace sdl assert by custom one
	SDL_assert(!_instance);

	_instance = this;
}

bool Engine::run() {
	auto current_time = start_time;

	bool keep_running = true;

	// initialization

	// update
	double accumulator = 0.0;
	while (keep_running) {
		keep_running = _main_window.update();

		auto new_time = Clock::now();

		double frame_time = std::chrono::duration_cast<std::chrono::duration<double>>(new_time - current_time).count();
		current_time = new_time;

		accumulator += frame_time;
		while (accumulator >= simulation_time) {
			accumulator -= simulation_time;
			// _physics_update here
		}

		// Update here

		// Tell the renderer to render here
	}

	return true;
}

} //namespace feather