#pragma once

#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <print>
#include <source_location>
#include <stdexcept>

// Diagnostics go to std::cerr, not std::cout: cout is fully buffered when not a
// tty and std::terminate() discards the buffer, so every assertion message was
// being swallowed -- an abort with no explanation. cerr is unit-buffered.
inline void fassert(bool condition, std::string message, std::source_location loc = std::source_location::current()) {
	if (!condition) {
		std::println(std::cerr, "Assertion failed ({}:{}) : {}", loc.file_name(), loc.line(), message);
		std::terminate();
	}
}

inline void fassert(bool condition, std::source_location loc = std::source_location::current()) {
	if (!condition) {
		std::println(std::cerr, "Assertion failed ({}:{})", loc.file_name(), loc.line());
		std::terminate();
	}
}