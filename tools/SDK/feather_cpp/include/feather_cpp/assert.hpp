#pragma once

// The engine's assertion, copied for plugin code.
//
// A copy rather than a binding: fassert is an inline function template-free
// header, and its default `std::source_location` argument only means anything
// when it is evaluated at the caller. Routed through a C entry point it would
// report the glue's own line, every time. Vendored for the same reason the
// SimpleMath sources are -- a plugin compiles it itself and gets the engine's
// behaviour exactly.
//
// KEEP IN SYNC with core/framework/assert.h.

#include <exception>
#include <source_location>
#include <string>

// std::println is C++23 and not in every standard library a plugin might build
// against yet; the fallback reports the same thing through stdio.
#if __has_include(<print>) && defined(__cpp_lib_print)
#define FEATHER_CPP_HAS_PRINT 1
#include <iostream>
#include <print>
#else
#define FEATHER_CPP_HAS_PRINT 0
#include <cstdio>
#endif

inline void fassert(bool condition, std::string message,
    std::source_location loc = std::source_location::current())
{
	if (!condition) {
#if FEATHER_CPP_HAS_PRINT
		std::println(std::cerr, "Assertion failed ({}:{}) : {}", loc.file_name(), loc.line(), message);
#else
		std::fprintf(stderr, "Assertion failed (%s:%u) : %s\n", loc.file_name(),
			static_cast<unsigned>(loc.line()), message.c_str());
#endif
		std::terminate();
	}
}

inline void fassert(bool condition, std::source_location loc = std::source_location::current())
{
	if (!condition) {
#if FEATHER_CPP_HAS_PRINT
		std::println(std::cerr, "Assertion failed ({}:{})", loc.file_name(), loc.line());
#else
		std::fprintf(stderr, "Assertion failed (%s:%u)\n", loc.file_name(),
			static_cast<unsigned>(loc.line()));
#endif
		std::terminate();
	}
}
