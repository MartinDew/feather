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

// The single spelling a resource is cached under. Both entry points into the
// cache -- load() and index_project() -- have to agree on it, or the same file
// reached two ways (a res:// path, the project walk, a manifest naming its own
// library) is loaded more than once.
static std::string cache_key(const Path& path) {
	std::error_code ec;
	auto canonical = std::filesystem::weakly_canonical(path, ec);
	// weakly_canonical only fails on something like a permission error partway
	// up the path; the raw string is still a usable key, just a pickier one.
	return ec ? path.string() : canonical.string();
}

std::shared_ptr<Resource> ResourceLoader::load(const Path& path) {
	auto extension = strip_extension(path);
	auto localized = ProjectSettings::get()->localize_path(path);
	auto key = cache_key(localized);

	auto it = get()->_path_cache.find(key);
	if (it != get()->_path_cache.end()) {
		auto res = it->second;
		if (!res->is_loaded()) {
			// Snapshot: a loader's load() can register another format loader
			// (ClassDB::on_subclass_registered, above), which appends to the
			// very vector being walked here and invalidates the iterators.
			auto loaders = get()->_format_loaders;
			for (const auto& loader : loaders) {
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

	auto loaders = get()->_format_loaders; // snapshot; see above
	for (const auto& loader : loaders) {
		if (loader->recognize_extension(extension)) {
			auto res = loader->instantiate(localized);
			if (!res)
				return nullptr;
			res->_rid = generate_rid();
			get()->_cache[res->_rid] = res;
			get()->_path_cache[key] = res;
			loader->load(res, localized);
			return res;
		}
	}

	std::cerr << "ResourceLoader: No loader for extension '" << extension << "' for resource: " << path << std::endl;
	return nullptr;
}

void ResourceLoader::add_resource_format_loader(std::shared_ptr<ResourceFormatLoader> loader) {
	_format_loaders.push_back(loader);
	++_loader_generation;
}

void ResourceLoader::alias_resource_path(const Path& path, std::shared_ptr<Resource> res) {
	get()->_path_cache[cache_key(path)] = std::move(res);
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

	std::vector<Path> pending;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(project_path)) {
		if (!entry.is_regular_file())
			continue;
		pending.push_back(entry.path());
	}

	// .fext manifests before anything else: a manifest is the authoritative
	// declaration of an extension, and claiming it first stops the shared
	// library it names from being opened again by the generic probe below.
	std::stable_partition(pending.begin(), pending.end(),
			[](const Path& p) { return strip_extension(p) == "fext"; });

	// An extension can register format loaders of its own, and those have to
	// get a look at files the walk has already passed -- so keep re-running
	// over what nothing claimed until a full pass registers no new loader.
	while (!pending.empty()) {
		auto generation_at_start = self._loader_generation;
		std::vector<Path> unclaimed;

		for (const auto& path : pending) {
			auto key = cache_key(path);
			if (self._path_cache.contains(key))
				continue;

			auto extension = strip_extension(path);
			bool claimed = false;

			// Snapshot: loading one resource can register a format loader,
			// which appends to _format_loaders mid-iteration.
			auto loaders = self._format_loaders;
			for (const auto& loader : loaders) {
				if (!loader->recognize_extension(extension))
					continue;

				auto res = loader->instantiate(path);
				if (!res) {
					// Loader explicitly declined (e.g. a DLL that is not an
					// extension). Another loader might still want it, but no
					// two loaders claim the same extension today.
					claimed = true;
					break;
				}

				res->_rid = generate_rid();
				self._cache[res->_rid] = res;
				self._path_cache[key] = res;
				++count;

				if (loader->requires_immediate_load()) {
					loader->load(res, path);
				}
				claimed = true;
				break;
			}

			if (!claimed)
				unclaimed.push_back(path);
		}

		if (self._loader_generation == generation_at_start)
			break; // nothing new can claim what's left

		pending = std::move(unclaimed);
	}

	std::println(std::cout, "ResourceLoader: Indexed {} project resources.", count);
}

} // namespace feather