#pragma once

#include <core/framework/path.h>

#include <memory>
#include <string_view>

namespace feather {

class Extension;
class SharedLibrary;

// The last step an extension goes through once its library is mapped.
//
// This does not touch Extension's private library handle -- the calling loader
// owns that, and is the friend that may set it.

// Resolves the extension's entry point in `lib` and hands it to the
// init-level machinery, which catches it up on every level already entered.
void resolve_and_register_extension_entry(const std::shared_ptr<Extension>& ext,
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name);

} // namespace feather
