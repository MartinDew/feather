#include "engine.h"
#include "launch_settings.h"
#include "project_settings.h"
#include "resources/resource_loader.h"

#include <framework/register_framework_types.gen.h>
#include <main/register_main_types.gen.h>
#include <math/register_math_types.gen.h>
#include <rendering/register_rendering_types.gen.h>
#include <resources/register_resources_types.gen.h>
#include <world/register_world_types.gen.h>

#include <modules/modules.gen.h>

namespace feather {

struct Main {
	ClassDB _class_db;
	LaunchSettings _launch_settings;
	ProjectSettings _project_settings;
	ResourceLoader _resource_loader;

	Main(int argc, char* argv[]);
	~Main();
	static void setup_db();
};

Main::Main(int argc, char* argv[]) : _class_db(), _launch_settings(std::move(argc), std::move(argv)) {
	if (!_project_settings.init() && !_launch_settings.demo_mode.Get())
		return;

	setup_db();

	Engine engine;

	engine.run();

	unregister_modules();
}

Main::~Main() {
	// Runs before any member destructs -- they still follow, in reverse
	// declaration order, once this body returns. Must clear ClassDB's
	// registry here, before _resource_loader's implicit destructor drops the
	// last reference to any loaded extension and unloads its DLL
	// (SharedLibrary::unload() -> SDL_UnloadObject()). bind_method/
	// bind_static_method are header templates, instantiated wherever a class
	// registers -- for a DLL-registered class that's inside the DLL itself,
	// so a stored Callable's std::function vtable can point into that DLL's
	// own compiled code. Destroying it after the DLL unloads calls through a
	// dangling vtable pointer into now-unmapped memory.
	ClassDB::clear();
}

void Main::setup_db() {
	register_framework_types();
	register_math_types();
	register_resources_types();
	register_rendering_types();
	register_world_types();
	register_main_types();

	// then register module types
	register_modules();
}

} //namespace feather

int main(int argc, char* argv[]) {
	feather::Main fmain(std::move(argc), std::move(argv));
}