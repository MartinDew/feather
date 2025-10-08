# toolchains/BUILD.bazel
package(default_visibility = ["//visibility:public"])

load(":cc_toolchain_config.bzl", "linux_gcc_toolchain_config", "linux_clang_toolchain_config", "windows_msvc_clangcl_toolchain_config", "windows_msvc_toolchain_config")

# This is a common toolchain type for all your C++ toolchains.
toolchain_type(name = "my_cpp_toolchain")

# ------------------------------------------------------------------------------
# LINUX
# ------------------------------------------------------------------------------

# Define the cc_toolchain rule for GCC on Linux
cc_toolchain(
    name = "linux_gcc",
    toolchain_identifier = "linux-x86_64-gcc",
    toolchain_config = ":linux_gcc_toolchain_config",
    all_files = ":empty",
    compiler_files = ":empty",
    linker_files = ":empty",
)

# Define the toolchain target for Bazel's resolution
toolchain(
    name = "linux_gcc_toolchain",
    toolchain = ":linux_gcc",
    toolchain_type = ":my_cpp_toolchain",
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

# Define the cc_toolchain rule for Clang on Linux
cc_toolchain(
    name = "linux_clang",
    toolchain_identifier = "linux-x86_64-clang",
    toolchain_config = ":linux_clang_toolchain_config",
    all_files = ":empty",
    compiler_files = ":empty",
    linker_files = ":empty",
)

# Define the toolchain target for Bazel's resolution
toolchain(
    name = "linux_clang_toolchain",
    toolchain = ":linux_clang",
    toolchain_type = ":my_cpp_toolchain",
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
)

# ------------------------------------------------------------------------------
# WINDOWS
# ------------------------------------------------------------------------------

# Define the cc_toolchain rule for Clang-CL on Windows
cc_toolchain(
    name = "windows_msvc_clangcl",
    toolchain_identifier = "windows-x86_64-msvc-clangcl",
    toolchain_config = ":windows_msvc_clangcl_toolchain_config",
    all_files = ":empty",
    compiler_files = ":empty",
    linker_files = ":empty",
)

# Define the toolchain target for Bazel's resolution
toolchain(
    name = "windows_msvc_clangcl_toolchain",
    toolchain = ":windows_msvc_clangcl",
    toolchain_type = ":my_cpp_toolchain",
    target_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
)

# Define the cc_toolchain rule for MSVC on Windows
cc_toolchain(
    name = "windows_msvc",
    toolchain_identifier = "windows-x86_64-msvc",
    toolchain_config = ":windows_msvc_toolchain_config",
    all_files = ":empty",
    compiler_files = ":empty",
    linker_files = ":empty",
)

# Define the toolchain target for Bazel's resolution
toolchain(
    name = "windows_msvc_toolchain",
    toolchain = ":windows_msvc",
    toolchain_type = ":my_cpp_toolchain",
    target_compatible_with = [
        "@platforms//os:windows",
        "@platforms//cpu:x86_64",
    ],
)

# This is a dummy filegroup required by the cc_toolchain rule
filegroup(name = "empty")


# ------------------------------------------------------------------------------
# APPLE (todo)
# ------------------------------------------------------------------------------


# ------------------------------------------------------------------------------
# WASM (todo)
# ------------------------------------------------------------------------------

# ------------------------------------------------------------------------------
# ANDROID (todo)
# ------------------------------------------------------------------------------