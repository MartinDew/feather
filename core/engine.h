#pragma once

#include "launch_settings.h"
#include "window.h"

#include <SDL3/SDL_assert.h>
#include <chrono>

namespace feather {

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

class Engine {
	Window _main_window;

	// the time accumulator
	TimePoint start_time = Clock::now();

	static Engine* _instance;

public:
	Engine();

	bool run();

	static Engine& get() { return *_instance; }

#ifndef EDITOR_MODE
	static constexpr bool is_editor() {
		return false;
	}
#else
	static bool is_editor() {
		return LaunchSettings::get().editor_mode.Get();
	}
#endif
};

} //namespace feather
