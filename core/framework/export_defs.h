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

// Hides a declaration from the binding generator without hiding it from the compiler (FEATHER_MRBIND_PARSE is defined only by
// the MRBind parse, so this costs nothing in a real build). For things that can't cross a C ABI even though they're public C++, e.g. a `static constexpr` member: binding one needs an exported definition to take the address of, which fails to link on Windows.
#if defined(FEATHER_MRBIND_PARSE)
#define FEATHER_NO_BIND [[clang::annotate("mrbind::ignore")]]
#else
#define FEATHER_NO_BIND
#endif
