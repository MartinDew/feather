#include "main.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
	SDL_Window* w = SDL_CreateWindow("Win", 1280, 720, SDL_WINDOW_RESIZABLE);

	return 0;
}