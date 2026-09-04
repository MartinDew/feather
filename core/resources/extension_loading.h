#pragma once

#include <core/framework/path.h>

#include <memory>
#include <string_view>

namespace feather {

class Extension;
class SharedLibrary;

// The last step an extension goes through once its library is mapped: resolves its entry point in `lib` and hands it to the
// init-level machinery, catching it up on every level already entered. Does not touch Extension's private library handle -- the calling loader owns that, and is the friend that may set it.
void resolve_and_register_extension_entry(const std::shared_ptr<Extension>& ext,
		const std::shared_ptr<SharedLibrary>& lib, const Path& path, std::string_view loader_name);

} // namespace feather
