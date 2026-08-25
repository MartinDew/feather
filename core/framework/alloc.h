#pragma once

// Routes every `new`/`delete` in every binary (engine + every consumer DLL)
// through feather_alloc/feather_free, whose only real implementation lives
// in feather.exe -- avoids the Windows static-CRT-per-binary heap mismatch,
// since the actual malloc()/free() always runs inside the engine's heap
// regardless of which binary's code triggered the call.
//
// The operator new/delete overrides are defined in alloc.cpp, not here:
// replaceable global allocation functions must not be `inline` (unlike
// ordinary overloads, C++17 didn't relax that for them). They're resolved
// by the linker against a well-known global symbol, not by textual
// visibility, so no file needs to #include this header for the override to
// apply -- alloc.cpp just needs to be compiled into each binary once.

#include "export_defs.h"

#include <cstddef>

extern "C" FEATHER_API void* feather_alloc(std::size_t size);
extern "C" FEATHER_API void feather_free(void* ptr) noexcept;
extern "C" FEATHER_API void* feather_alloc_aligned(std::size_t size, std::size_t alignment);
extern "C" FEATHER_API void feather_free_aligned(void* ptr, std::size_t alignment) noexcept;
