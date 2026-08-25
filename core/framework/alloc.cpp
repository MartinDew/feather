#include "alloc.h"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

// The real feather_alloc/feather_free bodies exist in exactly one binary:
// feather.exe. Every consumer DLL only ever sees these as dllimport (see
// export_defs.h) and calls through to this same implementation -- guarding
// on FEATHER_BUILDING_ENGINE keeps a consumer from trying to *define* a
// symbol it only has a dllimport declaration for, which doesn't compile.
//
// Plain malloc/free -- deliberately not `new`/`delete`, to avoid any
// question about interacting with the operator overrides below.
#if defined(FEATHER_BUILDING_ENGINE)

extern "C" void* feather_alloc(std::size_t size) {
	return std::malloc(size);
}

extern "C" void feather_free(void* ptr) noexcept {
	std::free(ptr);
}

extern "C" void* feather_alloc_aligned(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
	return _aligned_malloc(size, alignment);
#else
	// aligned_alloc requires size to be a multiple of alignment.
	std::size_t rounded_size = (size + alignment - 1) & ~(alignment - 1);
	return std::aligned_alloc(alignment, rounded_size);
#endif
}

extern "C" void feather_free_aligned(void* ptr, std::size_t alignment) noexcept {
	(void)alignment;
#if defined(_WIN32)
	_aligned_free(ptr);
#else
	std::free(ptr);
#endif
}

#endif // FEATHER_BUILDING_ENGINE

// Global operator new/delete overrides. Compiled into every binary -- the
// engine (xmake/engine.lua's CORE_SOURCES) and every consumer DLL
// (tools/SDK/FeatherSDK.lua's feather_sdk_setup) each add this same
// alloc.cpp to their own sources, so each binary gets exactly one
// definition, resolved by the linker against the standard global-allocation
// symbol regardless of which TU in that binary actually calls `new`/`delete`
// -- no header inclusion required at any of those call sites. Must NOT be
// `inline`: replaceable global allocation/deallocation functions are
// required to be non-inline (both clang and MSVC diagnose this).

void* operator new(std::size_t size) {
	if (void* ptr = feather_alloc(size)) {
		return ptr;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
	return ::operator new(size);
}

void operator delete(void* ptr) noexcept {
	feather_free(ptr);
}

void operator delete[](void* ptr) noexcept {
	feather_free(ptr);
}

// Sized-delete overloads (C++14) -- the compiler may call these instead of
// the unsized ones above; must be routed the same way.
void operator delete(void* ptr, std::size_t) noexcept {
	feather_free(ptr);
}

void operator delete[](void* ptr, std::size_t) noexcept {
	feather_free(ptr);
}

// ---- nothrow new/delete -----------------------------------------------------

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	return feather_alloc(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	return feather_alloc(size);
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
	feather_free(ptr);
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
	feather_free(ptr);
}

// ---- Aligned new/delete (C++17) --------------------------------------------

void* operator new(std::size_t size, std::align_val_t align) {
	if (void* ptr = feather_alloc_aligned(size, static_cast<std::size_t>(align))) {
		return ptr;
	}
	throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t align) {
	return ::operator new(size, align);
}

void operator delete(void* ptr, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

void operator delete[](void* ptr, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

void operator delete(void* ptr, std::size_t, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

void operator delete[](void* ptr, std::size_t, std::align_val_t align) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return feather_alloc_aligned(size, static_cast<std::size_t>(align));
}

void* operator new[](std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
	return feather_alloc_aligned(size, static_cast<std::size_t>(align));
}

void operator delete(void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}

void operator delete[](void* ptr, std::align_val_t align, const std::nothrow_t&) noexcept {
	feather_free_aligned(ptr, static_cast<std::size_t>(align));
}
