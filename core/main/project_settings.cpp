#include "project_settings.h"

#include "launch_settings.h"
#include <filesystem>
#include <iostream>

#if defined(_WIN32) || defined(_WIN64)
#include <shlobj.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace feather {

FSINGLETON_INSTANCE(ProjectSettings);

ProjectSettings::ProjectSettings() : _project_path(FileSystem::current_path()) {
	FSINGLETON_CONSTRUCT_INSTANCE()
}

bool ProjectSettings::init() {
	_project_path = LaunchSettings::get().project_path.Get();

	// Ensure the project path exists
	if (!std::filesystem::exists(_project_path)) {
		std::cerr << "Project path does not exist: " << _project_path.string() << std::endl;
		return false;
	}

	// Search for .fproj file
	for (const auto& entry : std::filesystem::directory_iterator(_project_path)) {
		if (entry.is_regular_file() && entry.path().extension() == ".fproj") {
			_project_name = entry.path().stem().string();
			break;
		}
	}

	if (_project_name.empty()) {
		std::cerr << "No .fproj file found in project directory: " << _project_path.string() << std::endl;
		return false;
	}

	return true;
}

std::string ProjectSettings::get_project_name() const {
	return _project_name;
}

static void replace_all(std::string& path, const std::string& token, const std::string& replacement) {
	size_t pos = 0;
	while ((pos = path.find(token, pos)) != std::string::npos) {
		path.replace(pos, token.length(), replacement);
		pos += replacement.length();
	}
}

Path ProjectSettings::localize_path(const Path& path) {
	std::string new_path = path.string();
	replace_all(new_path, "res://", get_project_path().string() + '/');

#if defined(_WIN32) || defined(_WIN64)
	char sys_root[MAX_PATH];
	GetSystemWindowsDirectoryA(sys_root, MAX_PATH);
	std::string root = std::string(1, sys_root[0]) + ":\\";
#else
	std::string root = "/";
#endif
	replace_all(new_path, "sys://", root);

#if defined(_WIN32) || defined(_WIN64)
	char home[MAX_PATH];
	std::string home_path =
			SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROFILE, NULL, 0, home)) ? std::string(home) + "\\" : "C:\\Users\\";
#elif defined(__APPLE__) || defined(__linux__)
	const char* home = std::getenv("HOME");
	if (!home) {
		struct passwd* pw = getpwuid(getuid());
		home = pw->pw_dir;
	}
	std::string home_path = std::string(home) + "/";
#endif
	replace_all(new_path, "home://", home_path);

	Path npath(new_path);
	npath.make_preferred();
	return npath;
}

} //namespace feather