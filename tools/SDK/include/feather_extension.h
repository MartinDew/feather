#pragma once

// The plugin-facing half of the extension ABI (plugin-abi-rework plan,
// Stage 6) -- the engine-facing half lives in
// core/resources/{extension_abi.h,extension_fingerprint.h,extension_registry.h}.
// Both engine and plugin see this same descriptor layout, but only the
// plugin side needs the macro below to actually produce one.

#include <resources/extension_abi.h>
#include <resources/extension_fingerprint.h>

// FEATHER_STATIC (Stage 7) collapses this to plain extern "C" linkage, same
// spirit as FEATHER_API/FEATHER_LOCAL doing the same in feather_api.h:
// a statically linked plugin has no module boundary to export across, so
// dllexport/visibility("default") would just needlessly widen the binary's
// export table.
#if defined(FEATHER_STATIC)
#	define FEATHER_EXTENSION_EXPORT extern "C"
#elif defined(_WIN32)
#	define FEATHER_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#	define FEATHER_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// feather_extension_main is the ONE fixed name ExtensionFormatLoader looks
// up in a dynamically loaded plugin, regardless of ident -- but under
// FEATHER_STATIC there is no dlopen at all (see
// ExtensionRegistry::submit_static_extensions), and a static executable may
// link more than one plugin, so a single fixed name across all of them
// would collide. Static mode's generated static_extensions.gen.cpp
// (feather_static_registry.lua) instead references each plugin's
// feather_extension_<ident>() directly by its own distinct name, which is
// why FEATHER_DEFINE_EXTENSION always emits that one regardless of mode --
// only this alias is mode-conditional.
#if defined(FEATHER_STATIC)
#	define FEATHER_DEFINE_EXTENSION_MAIN_ALIAS(ident)
#else
#	define FEATHER_DEFINE_EXTENSION_MAIN_ALIAS(ident)                                                          \
		FEATHER_EXTENSION_EXPORT const ::FeatherExtensionDesc* feather_extension_main() {                       \
			return feather_extension_##ident();                                                                 \
		}
#endif

// Declares this project's extension descriptor.
//
//   FEATHER_DEFINE_EXTENSION(mygame, "mygame", "my game's plugin",
//                             FEATHER_EXT_PRIORITY_DEFAULT,
//                             my_initialize, my_deinitialize);
//
// ident only needs to be a valid identifier, not globally unique within the
// engine as a whole -- but it DOES need to be unique among every plugin
// linked into the SAME static executable (see FEATHER_DEFINE_EXTENSION_MAIN_ALIAS
// above), so a game project with only one plugin can use anything, while a
// static build folding in several should give each a distinct ident.
//
// init_fn/deinit_fn must both be `void (*)(FeatherExtensionContext*)`;
// deinit_fn may be nullptr if the plugin has nothing to tear down beyond
// what ClassDB/ResourceLoader already handle automatically on unload
// (ExtensionRegistry::shutdown_all()).
#define FEATHER_DEFINE_EXTENSION(ident, ext_name, ext_description, ext_priority, init_fn, deinit_fn)        \
	FEATHER_EXTENSION_EXPORT const ::FeatherExtensionDesc* feather_extension_##ident() {                    \
		/* Field order must match FeatherExtensionDesc (extension_abi.h) exactly -- this is positional, */  \
		/* not designated, aggregate init. */                                                                \
		static const ::FeatherExtensionDesc desc = {                                                        \
			sizeof(::FeatherExtensionDesc),                                                                 \
			FEATHER_EXTENSION_ABI_VERSION,                                                                  \
			FEATHER_ENGINE_ABI_VERSION,                                                                     \
			::feather::compute_build_fingerprint(),                                                         \
			ext_name,                                                                                       \
			ext_description,                                                                                \
			ext_priority,                                                                                   \
			0u,                                                                                              \
			init_fn,                                                                                        \
			deinit_fn,                                                                                      \
			nullptr,                                                                                        \
		};                                                                                                   \
		return &desc;                                                                                       \
	}                                                                                                        \
	FEATHER_DEFINE_EXTENSION_MAIN_ALIAS(ident)
