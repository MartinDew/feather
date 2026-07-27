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

-- Toolchain selection is done via CLI flags, not options:
--   LLVM/Clang (non-Windows):  xmake f --toolchain=llvm
--   Clang-CL (Windows):        xmake f --toolchain=clang-cl
--   MinGW cross:               xmake f --toolchain=mingw --sdk=<path>
--   Android NDK:               xmake f --plat=android --ndk=<path> --ndk_sdkver=21
