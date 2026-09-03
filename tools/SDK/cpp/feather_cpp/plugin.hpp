#pragma once

// The entry point a native plugin exports, and the one piece of setup it needs.
//
// A .fext manifest names this function; the engine resolves it by name and
// calls it once per init level. Hand-written rather than generated: the
// descriptor describes the engine's API, not the shape of a plugin.

#include <feather_cpp/core.hpp>

#include <cstdint>

#if defined(_WIN32)
#define FEATHER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FEATHER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

// Defines the exported entry point `name`, forwarding to `func`, which takes a
// feather::InitLevel. Plugins are built with hidden visibility, so the
// attribute above is what makes the symbol findable at all.
//
// Installs the exception handler before the first call: without it an engine
// exception reaches the plugin as a default-constructed return value instead of
// a feather::Error.
#define FEATHER_PLUGIN_ENTRY(name, func)                                      \
    extern "C" FEATHER_PLUGIN_EXPORT void name(::std::uint8_t _level)         \
    {                                                                         \
        ::feather::detail::install_exception_handler();                       \
        func(static_cast<::feather::InitLevel>(_level));                      \
    }
