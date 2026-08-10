#pragma once

// The engine's declared plugin ABI surface. feather_public_api puts
// <engine>/ and <engine>/core/ on a project DLL's include path wholesale --
// there is no private header directory -- so every namespace-scope class,
// free function, and extern variable in a header under core/ carries either
// FEATHER_API (reachable from a project DLL) or FEATHER_INTERNAL (compiled
// into the engine but never meant to cross the boundary). See the
// plugin-abi-rework plan's "Stage 3" for the full rationale; tools/codegen's
// --require-export-macro enforces this for every FCLASS/FSTRUCT type.
//
// FEATHER_STATIC is Stage 7's shipping-build switch (a game project statically
// links the engine into one executable): with no plugin boundary to declare,
// both macros collapse to nothing rather than exporting/importing anything.
#if defined(FEATHER_STATIC)
#	define FEATHER_API
#	define FEATHER_LOCAL
#elif defined(_WIN32)
// FEATHER_BUILDING_CORE is set on the engine executable, every engine module,
// and simplemath -- i.e. everything that DEFINES the surface rather than
// just consuming it. A project DLL never defines it, so it always sees
// dllimport. Missing this on one of those targets gives a dllimport/
// dllexport mismatch at link time on MSVC -- loud, not silent.
#	if defined(FEATHER_BUILDING_CORE)
#		define FEATHER_API __declspec(dllexport)
#	else
#		define FEATHER_API __declspec(dllimport)
#	endif
#	define FEATHER_LOCAL
#else
#	define FEATHER_API __attribute__((visibility("default")))
#	define FEATHER_LOCAL __attribute__((visibility("hidden")))
#endif

// A distinct name from FEATHER_LOCAL even though it expands the same way:
// FEATHER_LOCAL marks "this has no reason to be exported", FEATHER_INTERNAL
// marks "this was deliberately kept off the plugin ABI" -- the codegen
// export-macro check (generate_reflection.py's --require-export-macro) reads
// as documentation either way, but the two questions are different, and a
// reader shouldn't have to guess which one a bare FEATHER_LOCAL meant.
#define FEATHER_INTERNAL FEATHER_LOCAL
