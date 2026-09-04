#pragma once

#include <cstdint>
#include <memory>

namespace feather {

class Extension;

// Registration levels, entered in ascending order at startup and left in reverse order at shutdown; everything that registers
// something picks the earliest level whose guarantee (what already exists when it is entered) covers what it needs:
enum class InitLevel : uint8_t {
	// ClassDB exists and nothing else does. Reflected type registration.
	Core = 0,
	// The engine's servers (rendering) are constructed and initialized.
	Servers,
	// The ECS world exists; components, systems and world modules.
	World,
	// Editor-only registration. Entered only when running as the editor,
	// so nothing a game build needs may be registered here.
	Editor,

	Max,
};

inline constexpr uint8_t INIT_LEVEL_COUNT = static_cast<uint8_t>(InitLevel::Max);

const char* to_string(InitLevel level);

// The signature every extension entry point must have. Called once per level
// the engine enters, ascending, and once per level it leaves, descending.
using ExtensionEntryFn = void (*)(InitLevel);

// The deepest level entered so far, or nothing before the first enter_init_level().
bool has_entered_init_level(InitLevel level);

// Enters `level`: registers every built-in module and every loaded extension at that level. Levels must be entered in ascending
// order, each exactly once (Editor may be skipped -- see above).
void enter_init_level(InitLevel level);

// Leaves `level`, unregistering the built-in modules registered at it.
void exit_init_level(InitLevel level);

// Leaves every entered level, deepest first.
void exit_all_init_levels();

// Called by FextFormatLoader once an extension's entry point resolves. The extension is caught up immediately: its entry point
// is called once for every level already entered, so one loaded mid-startup sees the same sequence as one there from the beginning.
void register_extension_entry(const std::shared_ptr<Extension>& extension, ExtensionEntryFn entry);

} //namespace feather
