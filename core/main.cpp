#include "main.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
	std::cout << "hello world\n";

	SDL_Window* w = SDL_CreateWindow("Win", 1280, 720, SDL_WINDOW_RESIZABLE);

	std::this_thread::sleep_for(std::chrono::seconds(5));

	return 0;
}