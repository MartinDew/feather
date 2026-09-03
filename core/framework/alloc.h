#pragma once

// Routes every `new`/`delete` in the engine through feather_alloc/feather_free.
// Engine-internal: a plugin compiles none of the engine's code, so it uses its
// own allocator and never frees anything the engine allocated.
//
// The operator new/delete overrides are defined in alloc.cpp, not here:
// replaceable global allocation functions must not be `inline` (unlike
// ordinary overloads, C++17 didn't relax that for them). They're resolved
// by the linker against a well-known global symbol, not by textual
// visibility, so no file needs to #include this header for the override to
// apply.

#include <cstddef>

extern "C" void* feather_alloc(std::size_t size);
extern "C" void feather_free(void* ptr) noexcept;
extern "C" void* feather_alloc_aligned(std::size_t size, std::size_t alignment);
extern "C" void feather_free_aligned(void* ptr, std::size_t alignment) noexcept;
