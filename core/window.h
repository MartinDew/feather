#pragma once

#include <SDL3/SDL_events.h>

struct SDL_Window;

namespace feather {

class Window {
	SDL_Window* _internal_window = nullptr;
	SDL_Event _internal_event;

public:
	Window();

	// Resolve events
	bool update();
};

} //namespace feather