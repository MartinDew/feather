#include "script_format_loader.h"

#include "script.h"
#include "script_extension_runner.h"

#include <filesystem>
#include <iostream>
#include <print>

namespace feather {

namespace {

// Extension -> the runner that handles it. One entry today; the mapping exists
// so adding a language is a line here plus a runner, not a new loader.
const char* language_for_extension(const std::string& extension) {
	if (extension == "fpy") {
		return "python";
	}
	return nullptr;
}

} //namespace

bool ScriptFormatLoader::recognize_extension(const std::string& extension) const {
	return language_for_extension(extension) != nullptr;
}

std::shared_ptr<Resource> ScriptFormatLoader::instantiate(const Path& path) {
	const auto extension = path.extension().string().substr(path.extension().string().empty() ? 0 : 1);
	const char* language = language_for_extension(extension);
	if (!language) {
		return nullptr;
	}

	auto script = std::make_shared<Script>(language);
	script->set_path(path);
	return script;
}

void ScriptFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	auto script = std::static_pointer_cast<Script>(resource);
	if (!script || script->_ran) {
		return;
	}

	const auto* runner = find_script_extension_runner(script->_language);
	if (!runner) {
		std::cerr << "ScriptFormatLoader: No runner for " << script->_language << " scripts";
		if (script->_language == "python") {
			std::cerr << " (configure with --enable_py_host=y)";
		}
		std::cerr << ": " << path << std::endl;
		return;
	}

	if (!(*runner)(path)) {
		std::cerr << "ScriptFormatLoader: Script failed to run: " << path << std::endl;
		return;
	}

	script->_ran = true;
	std::println(std::cout, "ScriptFormatLoader: Ran {} script {}", script->_language, path.string());
}

} // namespace feather
