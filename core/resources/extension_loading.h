#pragma once

#include <core/framework/path.h>

#include <memory>
#include <string_view>

namespace feather {

class Extension;
class SharedLibrary;

// The two steps every extension goes through once its library is mapped,
// shared by the loaders that can produce one: ExtensionFormatLoader, which
// discovers extensions by probing every shared library in the project, and
// FextFormatLoader, which is told about them by a manifest.
//
// Neither helper touches Extension's private library handle -- the calling
// loader owns that, and is the friend that may set it.

// Builds an Extension from a library exporting the legacy
// _load_extension/_destroy_extension pair. Returns nullptr when
// _load_extension is absent, which just means the library isn't a feather
// extension; anything else that goes wrong is reported against `loader_name`.
std::shared_ptr<Extension> probe_legacy_extension(
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name);

// Resolves the extension's entry point in `lib` and hands it to the
// init-level machinery, which catches it up on every level already entered.
void resolve_and_register_extension_entry(const std::shared_ptr<Extension>& ext,
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name);

} // namespace feather
