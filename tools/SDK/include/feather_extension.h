#pragma once

// The plugin-facing half of the extension ABI (plugin-abi-rework plan,
// Stage 6) -- the engine-facing half lives in
// core/resources/{extension_abi.h,extension_fingerprint.h,extension_registry.h}.
// Both engine and plugin see this same descriptor layout, but only the
// plugin side needs the macro below to actually produce one.

#include <resources/extension_abi.h>
#include <resources/extension_fingerprint.h>

#if defined(_WIN32)
#	define FEATHER_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#	define FEATHER_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Declares this project DLL's extension descriptor.
//
//   FEATHER_DEFINE_EXTENSION(mygame, "mygame", "my game's plugin",
//                             FEATHER_EXT_PRIORITY_DEFAULT,
//                             my_initialize, my_deinitialize);
//
// ident only needs to be a valid identifier, not globally unique -- it names
// the per-plugin feather_extension_<ident>() symbol below, which exists so a
// future static link (Stage 7) has a name distinct across plugins to pull
// one specific plugin's objects out of its archive. Nothing outside this
// translation unit needs to know ident: feather_extension_main, the second
// symbol this macro emits, is the ONE fixed name ExtensionFormatLoader
// actually looks up in a dynamically loaded plugin, regardless of ident.
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
	FEATHER_EXTENSION_EXPORT const ::FeatherExtensionDesc* feather_extension_main() {                        \
		return feather_extension_##ident();                                                                 \
	}
