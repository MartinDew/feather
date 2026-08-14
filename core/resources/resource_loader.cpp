#include "resource_loader.h"

#include <core/main/class_db.h>
#include <main/project_settings.h>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace feather {

FSINGLETON_INSTANCE(ResourceLoader)

ResourceLoader::ResourceLoader() {
	FSINGLETON_CONSTRUCT_INSTANCE();

	ClassDB::on_subclass_registered(ResourceFormatLoader::get_class_static(), [](std::string_view class_name) {
		auto loader = ClassDB::create_object<ResourceFormatLoader>(class_name);
		if (loader) {
			std::println(std::cout, "Registered resource format loader: {}", class_name);
			ResourceLoader::get()->add_resource_format_loader(std::shared_ptr<ResourceFormatLoader>(std::move(loader)));
		}
	});
};

RID ResourceLoader::generate_rid() {
	return RID { get()->m_counter.fetch_add(1, std::memory_order_relaxed) };
}

void ResourceLoader::register_resource(std::shared_ptr<Resource> res) {
	res->_rid = generate_rid();
	get()->_cache[res->_rid] = res;
}

static std::string strip_extension(const Path& path) {
	std::string ext = path.extension().string();
	if (!ext.empty() && ext[0] == '.')
		ext = ext.substr(1);
	return ext;
}

std::shared_ptr<Resource> ResourceLoader::load(const Path& path) {
	auto extension = strip_extension(path);
	auto localized = ProjectSettings::get()->localize_path(path);

	auto it = get()->_path_cache.find(path.string());
	if (it != get()->_path_cache.end()) {
		auto res = it->second;
		if (!res->is_loaded()) {
			for (const auto& loader : get()->_format_loaders) {
				if (loader->recognize_extension(extension)) {
					loader->load(res, localized);
					break;
				}
			}
		}
		return res;
	}

	if (extension.empty()) {
		std::cerr << "ResourceLoader: Cannot load resource without extension: " << path << std::endl;
		return nullptr;
	}

	for (const auto& loader : get()->_format_loaders) {
		if (loader->recognize_extension(extension)) {
			auto res = loader->instantiate(localized);
			if (!res)
				return nullptr;
			res->_rid = generate_rid();
			get()->_cache[res->_rid] = res;
			get()->_path_cache[path.string()] = res;
			loader->load(res, localized);
			return res;
		}
	}

	std::cerr << "ResourceLoader: No loader for extension '" << extension << "' for resource: " << path << std::endl;
	return nullptr;
}

void ResourceLoader::add_resource_format_loader(std::shared_ptr<ResourceFormatLoader> loader) {
	_format_loaders.push_back(loader);
}

void ResourceLoader::remove_resource_format_loader(std::shared_ptr<ResourceFormatLoader> loader) {
	auto& loaders = _format_loaders;
	auto it = std::find(loaders.begin(), loaders.end(), loader);
	if (it != loaders.end()) {
		loaders.erase(it);
	}
}

void ResourceLoader::index_project() {
	auto project_path = ProjectSettings::get()->get_project_path();
	if (project_path.empty() || !std::filesystem::exists(project_path))
		return;

	auto& self = *get();
	size_t count = 0;

	for (const auto& entry : std::filesystem::recursive_directory_iterator(project_path)) {
		if (!entry.is_regular_file())
			continue;

		Path path = entry.path();
		if (self._path_cache.contains(path.string()))
			continue;

		auto extension = strip_extension(path);

		for (const auto& loader : self._format_loaders) {
			if (!loader->recognize_extension(extension))
				continue;

			auto res = loader->instantiate(path);
			if (!res)
				break; // loader explicitly declined (e.g. DLL without feather_extension_init)

			res->_rid = generate_rid();
			self._cache[res->_rid] = res;
			self._path_cache[path.string()] = res;
			++count;

			if (loader->requires_immediate_load()) {
				loader->load(res, path);
			}
			break;
		}
	}

	std::println(std::cout, "ResourceLoader: Indexed {} project resources.", count);
}

} // namespace feather