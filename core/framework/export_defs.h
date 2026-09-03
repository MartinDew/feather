#pragma once

// Exports a hand-written C entry point from the engine binary. Export only:
// nothing imports through this header, because no plugin includes engine
// headers -- a plugin declares these itself, against the flat C ABI.
// _WIN32 not _MSC_VER: mingw needs this too.
#if defined(_WIN32)
#define FEATHER_C_ABI __declspec(dllexport)
#else
#define FEATHER_C_ABI __attribute__((visibility("default")))
#endif

// Hides a declaration from the binding generator without hiding it from the
// compiler. FEATHER_MRBIND_PARSE is defined only by the MRBind parse (see
// xmake/modules/feather_bindings.lua), so this costs nothing in a real build.
//
// Use it for things that cannot cross a C ABI even though they are public C++.
// The canonical case is a `static constexpr` member: binding one yields an
// accessor returning its address, and an inline constexpr variable has no
// exported definition to take the address of -- which fails to link on Windows,
// where the consumer must import every symbol by name.
#if defined(FEATHER_MRBIND_PARSE)
#define FEATHER_NO_BIND [[clang::annotate("mrbind::ignore")]]
#else
#define FEATHER_NO_BIND
#endif
