#pragma once

#include <framework/feather_api.h>
#include <framework/singleton_helpers.h>
#include <resources/extension_abi.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace feather {

class Extension;

// Owns the ordered list of loaded extensions and separates plugin DISCOVERY
// from INITIALIZATION. ExtensionFormatLoader::instantiate()/load() only
// submit() during ResourceLoader::index_project()'s directory walk;
// Engine::run() calls activate_all() once every project resource --
// including every project DLL -- has been discovered, so activation order
// is a property of (priority, name), never of directory-walk iteration
// order. See the plugin-abi-rework plan's Stage 6.
class FEATHER_API ExtensionRegistry {
	friend struct Main;
	FDECLARE_SINGLETON(ExtensionRegistry);

	ExtensionRegistry();

	std::vector<std::shared_ptr<Extension>> _pending; // submitted, not yet activated
	std::vector<std::shared_ptr<Extension>> _active; // activated, in activation order

public:
	void submit(std::shared_ptr<Extension> extension);

	// Constructs and submit()s an Extension per descriptor, for a game
	// project's statically linked-in plugins (plugin-abi-rework plan,
	// Stage 7) -- called once from Main's constructor with whatever
	// feather_main() was handed (nullptr/0 for the plain dev/editor
	// feather.exe, which links no statics at all). No abi_version/
	// engine_version/build_fingerprint validation here, unlike
	// ExtensionFormatLoader's dynamic (dlopen) path: a statically linked
	// extension is compiled into this SAME binary as the engine, so a
	// mismatch of the kind those checks catch is structurally impossible.
	// Each Extension's _library_handle stays null, matching a dynamically
	// loaded one in every other respect -- shutdown_all() dropping the last
	// reference to one is then simply a no-op instead of an unload.
	void submit_static_extensions(const FeatherExtensionFn* fns, size_t count);

	// Sorts _pending by (priority, name), moves each into _active, and
	// calls its initialize() hook (or, for a legacy _load_extension()-ABI
	// extension, its single named entry point) -- recording, via a ClassDB
	// snapshot diff, exactly which classes that call registered. Safe to
	// call more than once: only ever activates what's currently pending, so
	// a second index_project() pass that discovers no new extensions is a
	// no-op here too.
	void activate_all();

	// Runs deinitialize() for every active extension in reverse activation
	// order, then unregisters each extension's classes from ClassDB, then
	// clears ResourceLoader's caches, and only THEN releases this
	// registry's own shared_ptr references to each Extension. The actual
	// SharedLibrary unload happens whenever that drops the last reference
	// to it, which by this ordering is always after everything above --
	// fixing the crash documented on Main's member list (feather_main.cpp),
	// where _resource_loader used to be able to unload a plugin while
	// _class_db still held std::function closures whose code lived in it.
	void shutdown_all();
};

} // namespace feather
