#include "main.h"
#include "SDL3/SDL_video.h"
#include <SDL3/SDL.h>

int main(int argc, char* argv[]) {
	std::cout << "hello world\n";

	SDL_Window* w = SDL_CreateWindow("Win", 1280, 720, SDL_WINDOW_RESIZABLE);

	while (true) {
	}

	return 0;
}