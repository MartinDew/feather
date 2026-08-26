#include "init_level.h"

#include <framework/assert.h>
#include <resources/extension.h>

#include <modules/modules.gen.h>

#include <vector>

namespace feather {

namespace {

struct LoadedExtension {
	// Weak: an extension's entry point lives in its shared library, which the
	// Extension resource keeps mapped. Holding it weakly means an unloaded
	// extension drops out of the walk instead of leaving a dangling pointer.
	std::weak_ptr<Extension> extension;
	ExtensionEntryFn entry;
};

std::vector<LoadedExtension>& _loaded_extensions() {
	static std::vector<LoadedExtension> extensions;
	return extensions;
}

uint8_t _entered_mask = 0;

constexpr uint8_t _bit(InitLevel level) {
	return static_cast<uint8_t>(1u << static_cast<uint8_t>(level));
}

} //namespace

const char* to_string(InitLevel level) {
	switch (level) {
		case InitLevel::Core: return "Core";
		case InitLevel::Servers: return "Servers";
		case InitLevel::World: return "World";
		case InitLevel::Editor: return "Editor";
		case InitLevel::Max: break;
	}
	return "<invalid>";
}

bool has_entered_init_level(InitLevel level) {
	return level < InitLevel::Max && (_entered_mask & _bit(level)) != 0;
}

void enter_init_level(InitLevel level) {
	fassert(level < InitLevel::Max, "enter_init_level: InitLevel::Max is not a level");
	// Ascending order, each level once: everything registering at this level
	// relies on every earlier level's registrations already being in place.
	fassert(_entered_mask < _bit(level),
			std::format("enter_init_level: {} entered out of order or twice", to_string(level)));

	_entered_mask |= _bit(level);

	register_modules(level);

	std::erase_if(_loaded_extensions(), [](const LoadedExtension& loaded) { return loaded.extension.expired(); });
	for (const LoadedExtension& loaded : _loaded_extensions()) {
		loaded.entry(level);
	}
}

void exit_init_level(InitLevel level) {
	fassert(has_entered_init_level(level),
			std::format("exit_init_level: {} was never entered", to_string(level)));
	fassert(_entered_mask >> static_cast<uint8_t>(level) == 1,
			std::format("exit_init_level: {} left before a level entered after it", to_string(level)));

	// Extensions get no teardown call: unlike built-in modules they are loaded
	// through a resource whose lifetime the loader owns, and there is no
	// extension teardown ABI yet.
	unregister_modules(level);

	_entered_mask &= static_cast<uint8_t>(~_bit(level));
}

void exit_all_init_levels() {
	for (uint8_t i = INIT_LEVEL_COUNT; i-- > 0;) {
		const auto level = static_cast<InitLevel>(i);
		if (has_entered_init_level(level)) {
			exit_init_level(level);
		}
	}
}

void register_extension_entry(const std::shared_ptr<Extension>& extension, ExtensionEntryFn entry) {
	fassert(extension != nullptr);
	fassert(entry != nullptr);

	_loaded_extensions().push_back({ extension, entry });

	// Catch up: an extension discovered partway through startup still sees
	// every level, in order, so it can register at the same one it would have
	// had it been loaded before the engine started.
	for (uint8_t i = 0; i < INIT_LEVEL_COUNT; ++i) {
		const auto level = static_cast<InitLevel>(i);
		if (has_entered_init_level(level)) {
			entry(level);
		}
	}
}

} //namespace feather
