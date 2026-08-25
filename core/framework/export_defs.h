#pragma once

// Cross-DLL export macro; FEATHER_BUILDING_ENGINE (xmake/engine.lua) flips
// export/import. _WIN32 not _MSC_VER: mingw needs this too.
#if defined(_WIN32)
#if defined(FEATHER_BUILDING_ENGINE)
#define FEATHER_API __declspec(dllexport)
#else
#define FEATHER_API __declspec(dllimport)
#endif
#else
#define FEATHER_API __attribute__((visibility("default")))
#endif
