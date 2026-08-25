#include "alloc.h"

#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

// Guarded on FEATHER_BUILDING_ENGINE: a consumer only has the dllimport
// declaration for these, and defining them there wouldn't compile.
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

// Not `inline` -- replaceable global allocation/deallocation functions
// aren't allowed to be. Each binary (engine + every consumer DLL) compiles
// this file once, giving it exactly one definition per link unit.

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
