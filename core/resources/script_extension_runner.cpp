#include "script_extension_runner.h"

#include <unordered_map>

namespace feather {

namespace {

// Function-local so registration from a module's static initialization order
// can't race the map's own construction.
std::unordered_map<std::string, ScriptExtensionRunner>& runners() {
	static std::unordered_map<std::string, ScriptExtensionRunner> instance;
	return instance;
}

} // namespace

void register_script_extension_runner(std::string type, ScriptExtensionRunner runner) {
	runners()[std::move(type)] = std::move(runner);
}

const ScriptExtensionRunner* find_script_extension_runner(const std::string& type) {
	auto it = runners().find(type);
	return it == runners().end() ? nullptr : &it->second;
}

} // namespace feather
