#include "launch_settings.h"

#include "class_db.h"
#include "framework/assert.h"
#include "rendering/renderer.h"

#include <numeric>

namespace feather {

LaunchSettings* LaunchSettings::_instance = nullptr;

LaunchSettings::LaunchSettings()
		: renderer { _parser,
					 "renderer",
					 std::format("choice of the renderer backend : ( {})",
								 ClassDB::get_children_names_string(Renderer::get_class_static())),
					 { "renderer" },
					 "VexRenderer" } {
	fassert(!_instance, "Attempt to create Launchsettings but it already exists");
}

LaunchSettings::LaunchSettings(int argc, char* argv[]) : LaunchSettings() {
	fassert(!_instance, "LaunchSettings instance already exists!");

	_instance = this;

	_parser.ParseCLI(argc, argv);

	switch (_parser.GetError()) {
	case args::Error::Help:
		std ::cout << _parser;
		std::terminate();
		break;
	default:
		break;
	}
}

void LaunchSettings::init(int argc, char* argv[]) {
	fassert(_instance, "LaunchSettings instance does not exists!");

	_parser.ParseCLI(argc, argv);

	switch (_parser.GetError()) {
	case args::Error::Help:
		std ::cout << _parser;
		std::terminate();
		break;
	default:
		break;
	}
}

LaunchSettings& LaunchSettings::get() {
	if (!_instance)
		_instance = new LaunchSettings();

	return *_instance;
}

} //namespace feather