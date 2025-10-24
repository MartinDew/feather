#include "engine.h"
#include "launch_settings.h"

int main(int argc, char* argv[]) {
	feather::LaunchSettings launch_settings{ argc, argv };

	feather::Engine engine;

	engine.run();
}