#pragma once

// The allocation-neutral extension ABI (plugin-abi-rework plan, Stage 6).
// Pure C, no STL, no FEATHER_API: this header is included by the engine AND
// by every project DLL, on both sides of the plugin boundary, so it can't
// depend on anything that requires matching compiler/STL/CRT internals --
// that's exactly the class of mismatch decision 7's shared-runtime-mode
// requirement already narrows, but this header is the one piece that must
// stay safe even if that requirement is ever violated by a misconfigured
// plugin, since it's what lets the engine DETECT that case (build_fingerprint
// below) instead of silently corrupting the heap.
//
// The descriptor a plugin returns lives in its own static storage (see
// tools/SDK/include/feather_extension.h) -- the engine reads it and
// allocates its own feather::Extension with make_shared. Nothing is `new`'d
// in one module and `delete`'d in the other.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Struct layout version: bump whenever a field is added, removed, or
// reordered. struct_size (below) additionally lets the engine detect a
// plugin built against an OLDER layout than what a NEWER field addition
// expects, without needing a version bump for purely-additive changes.
#define FEATHER_EXTENSION_ABI_VERSION 1u

// Engine version: bumped whenever core's reflected ABI surface (the actual
// FEATHER_API classes/functions a plugin links against) changes in a way
// that isn't just a struct layout change -- e.g. a signature change on an
// existing FEATHER_API method. Independent of FEATHER_EXTENSION_ABI_VERSION
// above, which only versions this one struct.
#define FEATHER_ENGINE_ABI_VERSION 1u

enum {
	FEATHER_EXT_PRIORITY_CORE = -2000,
	FEATHER_EXT_PRIORITY_EDITOR = -1000,
	FEATHER_EXT_PRIORITY_DEFAULT = 0,
	FEATHER_EXT_PRIORITY_LATE = 1000,
};

// Deliberately opaque and never dereferenced by engine or plugin today --
// reserved for passing engine services into initialize()/deinitialize()
// once Stage 8 (editor-as-extension) needs it. A plugin's init/deinit
// functions must still accept the pointer (for signature compatibility with
// FeatherExtensionDesc below) even though nothing meaningful is behind it yet.
typedef struct FeatherExtensionContext FeatherExtensionContext;

typedef struct FeatherExtensionDesc {
	uint32_t struct_size; // sizeof(FeatherExtensionDesc) as the PLUGIN saw it -> forward compat
	uint32_t abi_version; // FEATHER_EXTENSION_ABI_VERSION the plugin was built against
	uint32_t engine_version; // FEATHER_ENGINE_ABI_VERSION the plugin was built against
	uint32_t build_fingerprint; // see extension_fingerprint.h -- catches CRT/debug-level mismatches
	const char* name; // static storage in the plugin; engine never frees
	const char* description;
	int32_t priority; // lower activates first; see FEATHER_EXT_PRIORITY_* above
	uint32_t flags; // reserved, always 0 for now
	void (*initialize)(FeatherExtensionContext*);
	void (*deinitialize)(FeatherExtensionContext*);
	const void* interface_table; // reserved; the seam if a C ABI is ever wanted
} FeatherExtensionDesc;

#ifdef __cplusplus
}
#endif
