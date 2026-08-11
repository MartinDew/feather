#include "engine.h"
#include "launch_settings.h"
#include "project_settings.h"
#include "resources/resource_loader.h"

#include <cstdio>

#include <framework/register_framework_types.gen.h>
#include <main/register_main_types.gen.h>
#include <math/register_math_types.gen.h>
#include <rendering/register_rendering_types.gen.h>
#include <resources/register_resources_types.gen.h>
#include <world/register_world_types.gen.h>

#include <modules/modules.gen.h>

namespace feather {

struct Main {
	// KNOWN ISSUE, not fixed here: _resource_loader owns the loaded project
	// DLL (via its Extension cache) and unloads it in its own destructor;
	// _class_db's _class_infos holds std::function closures
	// (ClassInfo::object_create_func) whose CODE lives in that DLL for every
	// project-defined type. Declaration order is destruction order,
	// reversed, so _resource_loader (declared last) unloads the plugin
	// BEFORE _class_db (declared first) tears down _class_infos -- calling
	// into already-unmapped memory. Confirmed with coredumpctl: SIGSEGV in
	// ClassInfo's std::function destructor, unwinding through
	// ClassDB::~ClassDB(), on a graceful shutdown (SIGTERM caught, engine.run()
	// returns normally) with a project loaded.
	//
	// Simply reordering these two members trades that crash for a WORSE one:
	// LaunchSettings::LaunchSettings() (launch_settings.cpp:17) calls
	// ClassDB::get_children_names() during its own construction, so _class_db
	// also has to be constructed FIRST -- and plain member ordering can't
	// give one member both "constructed first" and "destructed first" at
	// once (construction is forward, destruction is exactly reverse). The
	// real fix is an explicit teardown step in Main::~Main()'s body -- clear
	// _class_db's plugin-resident entries before _resource_loader unloads
	// anything -- which is Stage 6 (extension ABI / ordered teardown)
	// territory per the plugin-abi-rework plan, not a one-line reorder here.
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

Main::~Main() = default;

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
	// stdout is fully buffered the moment it isn't a tty (piped to a file, as
	// CI's smoke test does) -- an abrupt termination (a crash, or a CI
	// timeout's kill) discards whatever hadn't been flushed, silently eating
	// diagnostic std::cout output. Force line buffering unconditionally so
	// stdout behaves the same piped as it does on a terminal, on every
	// platform -- see framework/assert.h's comment for why fassert() itself
	// goes to the (always-unbuffered) cerr instead, which this doesn't
	// replace, only generalizes.
	std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);

	feather::Main fmain(std::move(argc), std::move(argv));
}