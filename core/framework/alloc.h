#pragma once

// Routes every `new`/`delete` in every binary (the engine exe and every
// consumer DLL) through the engine's own feather_alloc/feather_free, whose
// only real implementation lives in feather.exe (see alloc.cpp). This makes
// the Windows static-CRT-per-binary heap mismatch structurally impossible:
// no matter which binary's compiled code runs a `new` or `delete` expression,
// the actual malloc()/free() call always executes inside the engine's own
// CRT heap. Force-included into every translation unit in both the engine
// and every consumer build (xmake/engine.lua, tools/SDK/FeatherSDK.lua) via
// a compiler flag, not a manual #include, so no source file can skip it.
//
// extern "C" so a caller with a different name-mangling scheme (or no C++
// at all) can still resolve these symbols by name.
//
// The operator overloads are `inline` -- permitted for replaceable global
// allocation functions since C++17 -- specifically so this header can be
// force-included into every TU of a binary without multiple-definition
// errors; each binary still ends up with exactly one real feather_alloc/
// feather_free body, in the engine.

#include "export_defs.h"

#include <cstddef>
#include <new>

extern "C" FEATHER_API void* feather_alloc(std::size_t size);
extern "C" FEATHER_API void feather_free(void* ptr) noexcept;
extern "C" FEATHER_API void* feather_alloc_aligned(std::size_t size, std::size_t alignment);
extern "C" FEATHER_API void feather_free_aligned(void* ptr, std::size_t alignment) noexcept;

// ---- Ordinary new/delete ---------------------------------------------------

inline void* operator new(std::size_t size) {
	if (void* ptr = feather_alloc(size)) {
		return ptr;
	}
	throw std::bad_alloc();
}

inline void* operator new[](std::size_t size) {
	return ::operator new(size);
}

inline void operator delete(void* ptr) noexcept {
	feather_free(ptr);
}

inline void operator delete[](void* ptr) noexcept {
	feather_free(ptr);
}

// Sized-delete overloads (C++14) -- the compiler may call these instead of
// the unsized ones above; must be routed the same way.
inline void operator delete(void* ptr, std::size_t) noexcept {
	feather_free(ptr);
}

inline void operator delete[](void* ptr, std::size_t) noexcept {
	feather_free(ptr);
}

// ---- nothrow new/delete -----------------------------------------------------

inline void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	return feather_alloc(size);
}

inline void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	return feather_alloc(size);
}

inline void operator delete(void* ptr, const std::nothrow_t&) noexcept {
	feather_free(ptr);
}

inline void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	feather_free(ptr);
}

// ---- Aligned new/delete (C++17) --------------------------------------------

inline void* operator new(std::size_t size, std::align_val_t align) {
	if (void* ptr = feather_alloc_aligned(size, static_cast<std::size_t>(align))) {
		return ptr;
	}
	throw std::bad_alloc();
}

inline void* operator new[](std::size_t size, std::align_val_t align) {
	return ::operator new(size, align);
}

inline void operator delete(void* ptr, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

inline void operator delete[](void* ptr, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

inline void operator delete(void* ptr, std::size_t, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

inline void operator delete[](void* ptr, std::size_t, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

inline void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return feather_alloc_aligned(size, static_cast<std::size_t>(align));
}

inline void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return feather_alloc_aligned(size, static_cast<std::size_t>(align));
}

inline void operator delete(void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

inline void operator delete[](void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}
