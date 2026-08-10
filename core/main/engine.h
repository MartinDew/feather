#pragma once

#include "window.h"
#include "world_sim.h"

#include <rendering/rendering_server.h>

#include <chrono>

namespace feather {

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;

struct Main;

class FEATHER_API Engine {
	friend Main;
	static Engine* _instance;

	RenderingServer _rendering_server;
	Window _main_window;
	WorldSim _world_sim;

	// the time accumulator
	TimePoint start_time = Clock::now();
	double _current_dt = 0.0;

	Engine();

public:
	~Engine();

	bool run();

	static Engine& get() { return *_instance; }

	static bool is_editor();

	// Headless: no display server and no GPU device. Parsed once from
	// --window-mode; see the definition in engine.cpp.
	static bool is_headless();

	Window& get_main_window() { return _main_window; }

	double get_current_delta_time() const;

	static constexpr double simulation_time = 1.0 / 60.0;
};

} //namespace feather
