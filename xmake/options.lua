option("use_lto")
    set_default(false)
    set_description("Enable link-time optimization (ThinLTO with LLVM/Clang, full LTO with MSVC)")
option_end()

option("static_cpp")
    set_default(not is_plat("macosx"))
    set_description("Link the C++ standard library statically")
option_end()

option("static_deps")
    set_default(true)
    set_description("Build all third-party dependencies as static libraries")
option_end()

option("production")
    set_default(false)
    set_description("Enable production flags (implies use_lto, static_cpp, static_deps)")
option_end()

option("enable_sanitizers")
    set_default(false)
    set_description("Enable AddressSanitizer + UBSan (debug mode + LLVM/Clang only)")
option_end()

option("enable_clang_tidy")
    set_default(false)
    set_description("Run clang-tidy during compilation (requires compile_commands.json)")
option_end()

option("editor_build")
    set_default(true)
    set_description("Compile with editor support (EDITOR_BUILD=1); off builds a shipping-only binary (EDITOR_BUILD=0)")
option_end()

-- Bindings options live here rather than next to their modules
-- (modules/*_bindings/xmake.lua) because thirdparty/xmake.lua is included
-- first and gates the mrbind package on them -- building that package can
-- mean building LLVM from source, so a build with no bindings enabled must
-- not pull it in at all.

option("enable_c_bindings")
    set_default(true)
    set_description("Generate C bindings for the public API via MRBind (build/bindings/c)")
option_end()

option("enable_cs_bindings")
    set_default(true)
    set_description("Generate C# bindings from the C bindings' descriptor (build/bindings/csharp)")
option_end()

-- Ignored on windows/mingw, where it's treated as off: the module must be
-- built by a Clang in MSVC-compatible mode against the official Python's ABI,
-- which this repo's Windows toolchain setup doesn't cover yet (see
-- modules/py_bindings/xmake.lua). The platform check can't live in
-- set_default() -- is_plat() reads as false inside an option scope, whatever
-- the real platform is, which silently turns the option off everywhere.
option("enable_py_bindings")
    set_default(true)
    set_description("Build the pybind11 Python extension module (build/bindings/python); ignored on Windows")
option_end()

-- Toolchain selection is done via CLI flags, not options:
--   LLVM/Clang (non-Windows):  xmake f --toolchain=llvm
--   Clang-CL (Windows):        xmake f --toolchain=clang-cl
--   MinGW cross:               xmake f --toolchain=mingw --sdk=<path>
--   Android NDK:               xmake f --plat=android --ndk=<path> --ndk_sdkver=21
