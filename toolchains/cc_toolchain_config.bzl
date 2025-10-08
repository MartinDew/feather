load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl", "feature", "flag_group", "flag_set", "tool_path")

def _impl(ctx):
    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        toolchain_identifier = "k8-toolchain",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "k8",
        target_libc = "unknown",
        compiler = "clang",
        abi_version = "unknown",
        abi_libc_version = "unknown",
    )

cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {},
    provides = [CcToolchainConfigInfo],
)

# Base features for C++
def common_features():
    return [
        feature(
            name = "c++23_standard",
            enabled = True,
            flags = [
                flag_set(
                    actions = ["c++-compile"],
                    flag_groups = [
                        flag_group(
                            flags = ["-std=c++23", "/std:c++latest"],
                        ),
                    ],
                ),
            ],
        ),
        feature(
            name = "c++26_standard",
            enabled = True,
            flags = [
                flag_set(
                    actions = ["c++-compile"],
                    flag_groups = [
                        flag_group(
                            flags = ["-std=c++26", "/std:c++latest"],
                        ),
                    ],
                ),
            ],
        ),
        # Add other common features like 'fastbuild', 'dbg', 'opt', 'dynamic_linking', etc.
    ]

# ------------------------------------------------------------------------------
# LINUX TOOLCHAINS
# ------------------------------------------------------------------------------

def linux_gcc_toolchain_config():
    return cc_toolchain_config(
        name = "linux_gcc_toolchain",
        toolchain_identifier = "linux-x86_64-gcc",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "x86_64",
        target_libc = "glibc",
        compiler = "gcc",
        abi_version = "v2",
        tool_paths = [
            tool_path(name = "gcc", path = "/usr/bin/gcc"),
            tool_path(name = "ld", path = "/usr/bin/ld"),
            tool_path(name = "ar", path = "/usr/bin/ar"),
            tool_path(name = "cpp", path = "/usr/bin/cpp"),
            tool_path(name = "gcov", path = "/usr/bin/gcov"),
            tool_path(name = "strip", path = "/usr/bin/strip"),
            tool_path(name = "objcopy", path = "/usr/bin/objcopy"),
        ],
        cxx_builtin_include_directories = [
            # ⚠️ IMPORTANT: Replace these with your system's actual GCC include paths.
            "/usr/lib/gcc/x86_64-linux-gnu/13/include",
            "/usr/lib/gcc/x86_64-linux-gnu/13/include-fixed",
            "/usr/include/x86_64-linux-gnu",
            "/usr/include",
        ],
        unfiltered_compile_flags = ["-fPIC"],
        features = common_features() + [
            # Add GCC-specific features here
        ],
    )

def linux_clang_toolchain_config():
    return cc_toolchain_config(
        name = "linux_clang_toolchain",
        toolchain_identifier = "linux-x86_64-clang",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "x86_64",
        target_libc = "glibc",
        compiler = "clang",
        abi_version = "v2",
        tool_paths = [
            tool_path(name = "clang", path = "/usr/bin/clang"),
            tool_path(name = "ld", path = "/usr/bin/ld"),
            tool_path(name = "ar", path = "/usr/bin/ar"),
            tool_path(name = "cpp", path = "/usr/bin/cpp"),
            tool_path(name = "gcov", path = "/usr/bin/gcov"),
            tool_path(name = "strip", path = "/usr/bin/strip"),
            tool_path(name = "objcopy", path = "/usr/bin/objcopy"),
        ],
        cxx_builtin_include_directories = [
            # ⚠️ IMPORTANT: Replace with your system's actual Clang include paths.
            "/usr/lib/llvm-18/lib/clang/18/include",
            "/usr/include/x86_64-linux-gnu",
            "/usr/include",
        ],
        unfiltered_compile_flags = ["-fPIC"],
        features = common_features() + [
            # Add Clang-specific features here
        ],
    )

# ------------------------------------------------------------------------------
# WINDOWS TOOLCHAINS
# ------------------------------------------------------------------------------

def windows_msvc_clangcl_toolchain_config():
    return cc_toolchain_config(
        name = "windows_msvc_clangcl_toolchain",
        toolchain_identifier = "windows-x86_64-msvc-clangcl",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "x86_64",
        compiler = "clang-cl",
        abi_version = "v2",
        tool_paths = [
            tool_path(name = "clang-cl", path = "C:/Program Files/LLVM/bin/clang-cl.exe"),
            tool_path(name = "lld-link", path = "C:/Program Files/LLVM/bin/lld-link.exe"),
            tool_path(name = "lib", path = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/lib.exe"),
        ],
        cxx_builtin_include_directories = [
            # ⚠️ IMPORTANT: These paths must match your Visual Studio installation.
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/include",
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/atl/include",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um",
        ],
        unfiltered_compile_flags = [
            "/EHsc",
            "/nologo",
            "/W3",
        ],
        features = common_features() + [
            feature(name = "vc_msvc_runtime_library", enabled = True),
        ],
    )

def windows_msvc_toolchain_config():
    return cc_toolchain_config(
        name = "windows_msvc_toolchain",
        toolchain_identifier = "windows-x86_64-msvc",
        host_system_name = "local",
        target_system_name = "local",
        target_cpu = "x86_64",
        compiler = "msvc",
        abi_version = "v2",
        tool_paths = [
            tool_path(name = "cl", path = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/cl.exe"),
            tool_path(name = "link", path = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/link.exe"),
            tool_path(name = "lib", path = "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/bin/Hostx64/x64/lib.exe"),
        ],
        cxx_builtin_include_directories = [
            # ⚠️ IMPORTANT: These paths must match your Visual Studio installation.
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/include",
            "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.39.33519/atl/include",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared",
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um",
        ],
        unfiltered_compile_flags = [
            "/EHsc",
            "/nologo",
            "/W3",
        ],
        features = common_features() + [
            feature(name = "vc_msvc_runtime_library", enabled = True),
        ],
    )
