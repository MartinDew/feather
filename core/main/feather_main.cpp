#include "engine.h"
#include "launch_settings.h"
#include "project_settings.h"
#include "resources/resource_loader.h"

#include <extension/bridges/resource_format_loader_bridge.h>
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
	setup_db();

#if EDITOR_BUILD
	// Describes the engine's own reflected API, so it needs no project and
	// runs before Engine/RenderingServer exist -- no window, no renderer.
	if (!_launch_settings.dump_api.Get().empty()) {
		ClassDB::dump_api_json(_launch_settings.dump_api.Get());
		unregister_modules();
		return;
	}
#endif

	if (_project_settings.init() || _launch_settings.demo_mode.Get()) {
		Engine engine;
		engine.run();
	}

	unregister_modules();
}

Main::~Main() = default;

void Main::setup_db() {
	register_framework_types();
	register_math_types();
	register_resources_types();
	register_rendering_types();
	register_world_types();
	register_main_types();
	register_resource_format_loader_bridge();

	// then register module types
	register_modules();
}

} //namespace feather

int main(int argc, char* argv[]) {
	feather::Main fmain(std::move(argc), std::move(argv));
}