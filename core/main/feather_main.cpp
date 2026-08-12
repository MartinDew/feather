#include "engine.h"
#include "feather_main.h"
#include "launch_settings.h"
#include "project_settings.h"
#include "resources/extension_registry.h"
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
	// _resource_loader owns the loaded project DLL (via its Extension
	// cache) and, left to plain member destruction order, would unload it
	// before _class_db tears down _class_infos -- whose
	// ClassInfo::object_create_func closures are code compiled INTO that
	// DLL for every project-defined type. Declaration order is
	// construction order, reversed for destruction, so _resource_loader
	// (declared last) would destruct FIRST. Confirmed with coredumpctl
	// before the fix below existed: SIGSEGV in ClassInfo's std::function
	// destructor, unwinding through ClassDB::~ClassDB(), on a graceful
	// shutdown (SIGTERM caught, engine.run() returns normally) with a
	// project loaded.
	//
	// Simply reordering these members would trade that crash for a WORSE
	// one: LaunchSettings::LaunchSettings() (launch_settings.cpp:17) calls
	// ClassDB::get_children_names() during its own construction, so
	// _class_db also has to be constructed FIRST -- and plain member
	// ordering can't give one member both "constructed first" and
	// "destructed first" at once (construction is forward, destruction is
	// exactly reverse). The actual fix (plugin-abi-rework plan, Stage 6) is
	// the explicit ExtensionRegistry::shutdown_all() call in ~Main()'s body
	// below: it unregisters every extension's ClassDB entries and clears
	// _resource_loader's caches BEFORE releasing the SharedLibrary handles
	// that keep each plugin mapped, all before any member's own destructor
	// runs (member destructors always run after the body finishes).
	ClassDB _class_db;
	LaunchSettings _launch_settings;
	ProjectSettings _project_settings;
	ResourceLoader _resource_loader;
	ExtensionRegistry _extension_registry;

	Main(int argc, char* argv[], const FeatherExtensionFn* statics, size_t statics_count);
	~Main();
	static void setup_db();
};

Main::Main(int argc, char* argv[], const FeatherExtensionFn* statics, size_t statics_count)
		: _class_db(), _launch_settings(std::move(argc), std::move(argv)) {
	if (!_project_settings.init() && !_launch_settings.demo_mode.Get())
		return;

	setup_db();

	// Submitted before Engine::run() rather than from inside it: run() calls
	// ExtensionRegistry::activate_all() right after index_project() (which
	// discovers and submit()s any DYNAMICALLY loaded extension), and
	// activate_all() processes everything pending together regardless of
	// how each entry got there -- so run() itself needs no static-vs-dynamic
	// branching at all. See ExtensionRegistry::submit_static_extensions.
	ExtensionRegistry::get()->submit_static_extensions(statics, statics_count);

	Engine engine;

	engine.run();

	unregister_modules();
}

Main::~Main() {
	ExtensionRegistry::get()->shutdown_all();
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

int feather_main(int argc, char* argv[], const FeatherExtensionFn* statics, size_t statics_count) {
	// stdout is fully buffered the moment it isn't a tty (piped to a file, as
	// CI's smoke test does) -- an abrupt termination (a crash, or a CI
	// timeout's kill) discards whatever hadn't been flushed, silently eating
	// diagnostic std::cout output. Force line buffering unconditionally so
	// stdout behaves the same piped as it does on a terminal, on every
	// platform -- see framework/assert.h's comment for why fassert() itself
	// goes to the (always-unbuffered) cerr instead, which this doesn't
	// replace, only generalizes.
	//
	// _IOLBF is a no-op on Windows: the MSVC CRT does not implement true
	// line buffering for non-console streams and silently treats _IOLBF the
	// same as _IOFBF, so a file-redirected stdout (exactly CI's smoke-test
	// setup) keeps batching writes into one large buffer regardless of this
	// call. Confirmed directly in CI: a poll loop that streamed smoke.log's
	// growth with real per-second timestamps showed zero new bytes for over
	// four minutes, then the entire remaining backlog appearing in one shot
	// at the exact instant the process was killed -- the CRT's buffer, not
	// engine's actual runtime, was the bottleneck. _IONBF (fully unbuffered)
	// is the only mode Windows actually honors per-write, so use that there;
	// _IOLBF stays on POSIX platforms where it works as documented and is
	// cheaper (flushes per line, not per write() call).
#ifdef _WIN32
	std::setvbuf(stdout, nullptr, _IONBF, 0);
#else
	std::setvbuf(stdout, nullptr, _IOLBF, BUFSIZ);
#endif

	Main fmain(std::move(argc), std::move(argv), statics, statics_count);
	return 0;
}

} //namespace feather

// Absent under FEATHER_STATIC: a static shipping build's own generated
// static_main.gen.cpp (feather_static_registry.lua) defines main() instead,
// passing its statically linked-in extensions through to feather_main() --
// this file's own CORE_SOURCES membership is shared between that build and
// the plain dev/editor feather.exe below (see xmake/core_sources.lua), so
// without this guard both main()s would link into the same static binary.
#ifndef FEATHER_STATIC
int main(int argc, char* argv[]) {
	return feather::feather_main(argc, argv);
}
#endif