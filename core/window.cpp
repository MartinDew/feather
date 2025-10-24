#include "window.h"

#include "SDL3/SDL_init.h"
#include "engine.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <cassert>
#include <iostream>

namespace feather {

Window::Window() {
	if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
		std::cerr << SDL_GetError() << std::endl;
		assert(false);
	}

	const SDL_DisplayMode* dm = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());

	if (!dm) {
		std::cerr << SDL_GetError() << std::endl;
		assert(false);
	}

	auto w{ dm->w / 2 };
	auto h{ dm->h / 2 };

	auto window_flags = SDL_WINDOW_MOUSE_CAPTURE;

	if (Engine::is_editor()) {
		window_flags |= SDL_WINDOW_RESIZABLE;
	}

	_internal_window = SDL_CreateWindow("Feather", w, h, window_flags);
}

bool Window::update() {
	while (SDL_PollEvent(&_internal_event)) {
		switch (_internal_event.type) {
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			SDL_LogDebug(0, "Close window requested");
			return false;
		case SDL_EVENT_WINDOW_RESIZED:
			break;
		}
	}

	return true;
}

} //namespace feather
