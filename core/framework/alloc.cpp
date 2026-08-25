#include "alloc.h"

#include <cstdlib>

#if defined(_WIN32)
#include <malloc.h>
#endif

// Plain malloc/free -- deliberately not `new`/`delete`, even though this TU
// also sees alloc.h's operator overrides: recursing through them here would
// be harmless (they'd just call back into these same functions) but there's
// no reason to route through the indirection twice.

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
