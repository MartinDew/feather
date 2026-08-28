#include "fext_format_loader.h"

#include "extension.h"
#include "extension_loading.h"
#include "resource_loader.h"
#include "script_extension_runner.h"

#include <framework/shared_library.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>

namespace feather {

namespace {

// The key a manifest's "libraries" table must use for this build. Kept
// deliberately coarse -- os.arch -- since that is what decides which binary
// can be loaded at all.
constexpr const char* PLATFORM_KEY =
#if defined(_WIN32)
		"windows."
#elif defined(__APPLE__)
		"macos."
#elif defined(__linux__)
		"linux."
#else
		"unknown."
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
		"arm64";
#elif defined(__x86_64__) || defined(_M_X64)
		"x86_64";
#else
		"unknown";
#endif

// Manifest-relative, so a project can be moved or checked out anywhere.
Path resolve_relative(const Path& manifest_path, const std::string& relative) {
	return manifest_path.parent_path() / relative;
}

} // namespace

bool FextFormatLoader::recognize_extension(const std::string& extension) const {
	return extension == "fext";
}

std::shared_ptr<Resource> FextFormatLoader::instantiate(const Path& path) {
	std::ifstream file(path);
	if (!file) {
		std::cerr << "FextFormatLoader: Cannot open manifest: " << path << std::endl;
		return nullptr;
	}

	nlohmann::json manifest;
	try {
		file >> manifest;
	} catch (const nlohmann::json::exception& e) {
		std::cerr << "FextFormatLoader: Invalid JSON in " << path << ": " << e.what() << std::endl;
		return nullptr;
	}

	// Only the field that decides how to read everything else is checked
	// against a hard-coded value; unknown *newer* minor additions stay
	// forward-compatible by being ignored.
	const auto version = manifest.value("fext_version", 0);
	if (version != 1) {
		std::cerr << "FextFormatLoader: Unsupported fext_version " << version << " in " << path
				  << " (this engine supports 1)" << std::endl;
		return nullptr;
	}

	const auto name = manifest.value("name", std::string {});
	if (name.empty()) {
		std::cerr << "FextFormatLoader: Manifest has no \"name\": " << path << std::endl;
		return nullptr;
	}

	const auto type = manifest.value("type", std::string { "native" });

	if (type != "native") {
		// A scripted extension: no library to load, no entry point to resolve.
		// Whichever module can run this type registered a runner at
		// InitLevel::Core; core itself knows nothing about interpreters.
		const auto* runner = find_script_extension_runner(type);
		if (!runner) {
			std::cerr << "FextFormatLoader: Extension '" << name << "' is type \"" << type
					  << "\", which this build has no runner for";
			if (type == "python") {
				std::cerr << " (configure with --enable_py_host=y)";
			}
			std::cerr << ": " << path << std::endl;
			return nullptr;
		}

		const auto script = manifest.value("script", std::string {});
		if (script.empty()) {
			std::cerr << "FextFormatLoader: Scripted extension '" << name << "' has no \"script\": " << path
					  << std::endl;
			return nullptr;
		}

		auto script_path = resolve_relative(path, script);
		if (!std::filesystem::exists(script_path)) {
			std::cerr << "FextFormatLoader: Extension '" << name << "' names a script that doesn't exist: "
					  << script_path << " (from " << path << ")" << std::endl;
			return nullptr;
		}

		if (!(*runner)(script_path)) {
			std::cerr << "FextFormatLoader: Extension '" << name << "' failed to run: " << script_path << std::endl;
			return nullptr;
		}

		// Recorded as a resource so the manifest counts as indexed and isn't
		// picked up again. It carries no library and no entry point: a script
		// runs once, here, rather than being called back per init level.
		auto script_ext = std::make_shared<Extension>(name, std::string_view {});
		script_ext->set_path(path);
		std::println(std::cout, "FextFormatLoader: Ran {} extension '{}' from {}", type, name, script_path.string());
		return script_ext;
	}

	if (!manifest.contains("libraries") || !manifest["libraries"].is_object()) {
		std::cerr << "FextFormatLoader: Native extension has no \"libraries\" table: " << path << std::endl;
		return nullptr;
	}

	const auto& libraries = manifest["libraries"];
	auto entry_it = libraries.find(PLATFORM_KEY);
	if (entry_it == libraries.end()) {
		std::cerr << "FextFormatLoader: Extension '" << name << "' has no library for platform " << PLATFORM_KEY
				  << ": " << path << std::endl;
		return nullptr;
	}

	auto library_path = resolve_relative(path, entry_it->get<std::string>());
	if (!std::filesystem::exists(library_path)) {
		std::cerr << "FextFormatLoader: Extension '" << name << "' names a library that doesn't exist: "
				  << library_path << " (from " << path << ")" << std::endl;
		return nullptr;
	}

	auto lib = std::make_shared<SharedLibrary>();
	if (!lib->load(library_path.string())) {
		std::cerr << "FextFormatLoader: Failed to load " << library_path << ": " << SharedLibrary::get_last_error()
				  << std::endl;
		return nullptr;
	}

	std::shared_ptr<Extension> ext;
	const auto entry_point = manifest.value("entry", std::string {});
	if (entry_point.empty()) {
		// No entry named: the library is expected to hand back an Extension
		// the old way. Lets a C++ extension adopt a manifest -- for the
		// explicit declaration and the skipped probing -- without changing
		// any of its code.
		ext = probe_legacy_extension(lib, library_path, "FextFormatLoader");
		if (!ext) {
			std::cerr << "FextFormatLoader: Extension '" << name
					  << "' declares no \"entry\" and its library exports no _load_extension: " << library_path
					  << std::endl;
			return nullptr;
		}
	} else {
		ext = std::make_shared<Extension>(name, entry_point);
	}

	ext->_library_handle = lib;
	ext->set_path(path);

	// The project walk would otherwise reach this library on its own and try
	// to open it a second time. Harmless for a manifest-style plugin, which
	// exports no _load_extension for the probe to find, but a C++ extension
	// using a manifest for its declaration would genuinely load twice.
	ResourceLoader::get()->alias_resource_path(library_path, ext);

	return ext;
}

void FextFormatLoader::load(std::shared_ptr<Resource> resource, const Path& path) {
	auto ext = std::static_pointer_cast<Extension>(resource);
	if (!ext->_library_handle) {
		// A scripted extension; it already ran in instantiate().
		return;
	}
	resolve_and_register_extension_entry(ext, ext->_library_handle, path, "FextFormatLoader");
}

} // namespace feather
