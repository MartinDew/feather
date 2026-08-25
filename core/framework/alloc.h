#pragma once

// Routes every `new`/`delete` in every binary (the engine exe and every
// consumer DLL) through the engine's own feather_alloc/feather_free, whose
// only real implementation lives in feather.exe. This makes the Windows
// static-CRT-per-binary heap mismatch structurally impossible: no matter
// which binary's compiled code runs a `new` or `delete` expression, the
// actual malloc()/free() call always executes inside the engine's own CRT
// heap.
//
// The global operator new/delete overrides themselves are defined in
// alloc.cpp, NOT here as inline functions -- replaceable global allocation
// functions must not be declared `inline` (clang/MSVC both reject it; this
// isn't the ordinary-overload C++17 relaxation). Because they're resolved by
// the linker against the well-known global symbol, not by textual visibility
// in each TU, no source file needs to #include this header for the override
// to take effect -- alloc.cpp just needs to be compiled into every binary
// (xmake/engine.lua's CORE_SOURCES, tools/SDK/FeatherSDK.lua's
// feather_sdk_setup) exactly once each.
//
// extern "C" so a caller with a different name-mangling scheme (or no C++
// at all) can still resolve these symbols by name.

#include "export_defs.h"

#include <cstddef>

extern "C" FEATHER_API void* feather_alloc(std::size_t size);
extern "C" FEATHER_API void feather_free(void* ptr) noexcept;
extern "C" FEATHER_API void* feather_alloc_aligned(std::size_t size, std::size_t alignment);
extern "C" FEATHER_API void feather_free_aligned(void* ptr, std::size_t alignment) noexcept;
