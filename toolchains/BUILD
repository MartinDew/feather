load(":cc_toolchain_config.bzl", "cc_toolchain_config")
load(":toolchain_helper.bzl",
    "get_msvc_tool_paths",
    "get_msvc_paths",
    "get_clang_windows_tool_paths",
    "get_clang_windows_include_dirs",
    "get_clang_linux_tool_paths",
    "get_clang_linux_paths",
    "get_gcc_linux_tool_paths",
    "get_gcc_linux_paths",
)

package(default_visibility = ["//visibility:public"])

# Linux x86_64 - Clang
toolchain(
    name = "clang_linux_x86_64_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    toolchain = ":clang_linux_x86_64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "clang_linux_x86_64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":clang_linux_x86_64_config",
    toolchain_identifier = "clang-linux-x86_64",
)

cc_toolchain_config(
    name = "clang_linux_x86_64_config",
    compiler = "clang",
    cpu = "x86_64",
    cxx_builtin_include_directories = get_clang_linux_paths()["include_dirs"],
    host_system_name = "linux",
    target_libc = "glibc",
    target_system_name = "linux",
    tool_paths = get_clang_linux_tool_paths(),
)

# Linux arm64 - Clang
toolchain(
    name = "clang_linux_arm64_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:arm64",
    ],
    toolchain = ":clang_linux_arm64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "clang_linux_arm64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":clang_linux_arm64_config",
    toolchain_identifier = "clang-linux-arm64",
)

cc_toolchain_config(
    name = "clang_linux_arm64_config",
    compiler = "clang",
    cpu = "arm64",
    cxx_builtin_include_directories = get_clang_linux_paths()["include_dirs"],
    host_system_name = "linux",
    target_libc = "glibc",
    target_system_name = "linux",
    tool_paths = get_clang_linux_tool_paths(),
)

# Linux x86_64 - GCC
toolchain(
    name = "gcc_linux_x86_64_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    toolchain = ":gcc_linux_x86_64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "gcc_linux_x86_64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":gcc_linux_x86_64_config",
    toolchain_identifier = "gcc-linux-x86_64",
)

cc_toolchain_config(
    name = "gcc_linux_x86_64_config",
    compiler = "gcc",
    cpu = "x86_64",
    cxx_builtin_include_directories = get_gcc_linux_paths()["include_dirs"],
    host_system_name = "linux",
    target_libc = "glibc",
    target_system_name = "linux",
    tool_paths = get_gcc_linux_tool_paths(),
)

# Linux arm64 - GCC
toolchain(
    name = "gcc_linux_arm64_toolchain",
    exec_compatible_with = [
        "@platforms//os:linux",
    ],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:arm64",
    ],
    toolchain = ":gcc_linux_arm64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "gcc_linux_arm64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":gcc_linux_arm64_config",
    toolchain_identifier = "gcc-linux-arm64",
)

cc_toolchain_config(
    name = "gcc_linux_arm64_config",
    compiler = "gcc",
    cpu = "arm64",
    cxx_builtin_include_directories = get_gcc_linux_paths()["include_dirs"],
    host_system_name = "linux",
    target_libc = "glibc",
    target_system_name = "linux",
    tool_paths = get_gcc_linux_tool_paths(),
)

# Windows x86_64 - Clang
toolchain(
    name = "clang_windows_x86_64_toolchain",
    exec_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    toolchain = ":clang_windows_x86_64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "clang_windows_x86_64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":clang_windows_x86_64_config",
    toolchain_identifier = "clang-windows-x86_64",
)

cc_toolchain_config(
    name = "clang_windows_x86_64_config",
    compiler = "clang-cl",
    cpu = "x86_64",
    cxx_builtin_include_directories = [
        "C:/Program Files/LLVM/lib/clang/18/include",
        "C:/Program Files/LLVM/lib/clang/17/include",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/include",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/include",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0/ucrt",
    ],
    host_system_name = "windows",
    target_libc = "msvcrt",
    target_system_name = "windows",
    tool_paths = {
        "ar": "C:/Program Files/LLVM/bin/llvm-ar.exe",
        "cpp": "C:/Program Files/LLVM/bin/clang-cl.exe",
        "gcc": "C:/Program Files/LLVM/bin/clang-cl.exe",
        "gcov": "C:/Program Files/LLVM/bin/llvm-cov.exe",
        "ld": "C:/Program Files/LLVM/bin/lld-link.exe",
        "nm": "C:/Program Files/LLVM/bin/llvm-nm.exe",
        "objdump": "C:/Program Files/LLVM/bin/llvm-objdump.exe",
        "strip": "C:/Program Files/LLVM/bin/llvm-strip.exe",
    },
)

# Windows x86_64 - MSVC
toolchain(
    name = "msvc_windows_x86_64_toolchain",
    exec_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    target_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
    toolchain = ":msvc_windows_x86_64",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)

cc_toolchain(
    name = "msvc_windows_x86_64",
    all_files = ":empty",
    compiler_files = ":empty",
    dwp_files = ":empty",
    linker_files = ":empty",
    objcopy_files = ":empty",
    strip_files = ":empty",
    supports_param_files = 1,
    toolchain_config = ":msvc_windows_x86_64_config",
    toolchain_identifier = "msvc-windows-x86_64",
)

cc_toolchain_config(
    name = "msvc_windows_x86_64_config",
    compiler = "msvc",
    cpu = "x86_64",
    cxx_builtin_include_directories = [
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/include",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.39.33519/include",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared",
        "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22000.0/ucrt",
    ],
    host_system_name = "windows",
    target_libc = "msvcrt",
    target_system_name = "windows",
    tool_paths = {
        "ar": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/lib.exe",
        "cpp": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe",
        "gcc": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe",
        "gcov": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe",
        "ld": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/link.exe",
        "nm": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/dumpbin.exe",
        "objdump": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/dumpbin.exe",
        "strip": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe",
    },
)

filegroup(
    name = "empty",
    srcs = [],
)