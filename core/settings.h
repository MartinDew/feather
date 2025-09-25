#pragma once

#include <cstddef>
#include <filesystem>

struct LaunchSettings {
	std::filesystem::path project_path = std::filesystem::current_path();

	bool editor_mode = false;

	const LaunchSettings& Get();

private:
	static LaunchSettings* instance;

	LaunchSettings(size_t argc, char* argv[]);
};