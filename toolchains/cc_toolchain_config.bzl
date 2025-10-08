load("@bazel_tools//tools/cpp:cc_toolchain_config_lib.bzl",
    "action_config",
    "feature",
    "feature_set",
    "flag_group",
    "flag_set",
    "tool",
    "tool_path",
    "with_feature_set",
)
load("@bazel_tools//tools/build_defs/cc:action_names.bzl", "ACTION_NAMES")

def _impl(ctx):
    tool_paths = [
        tool_path(name = name, path = path)
        for name, path in ctx.attr.tool_paths.items()
    ]

    compiler = ctx.attr.compiler
    cpu = ctx.attr.cpu

    # Common feature for C++23/26 support
    cpp_standard_feature = feature(
        name = "cpp_standard",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.cpp_compile,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_cpp_standard_flags(compiler),
                    ),
                ],
            ),
        ],
    )

    # Compiler-specific flags
    default_compile_flags_feature = feature(
        name = "default_compile_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_compile_flags(compiler, cpu),
                    ),
                ],
            ),
        ],
    )

    # Linker flags
    default_link_flags_feature = feature(
        name = "default_link_flags",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.cpp_link_executable,
                    ACTION_NAMES.cpp_link_dynamic_library,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_link_flags(compiler, cpu),
                    ),
                ],
            ),
        ],
    )

    # Optimization features
    opt_feature = feature(
        name = "opt",
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_opt_flags(compiler),
                    ),
                ],
            ),
        ],
    )

    dbg_feature = feature(
        name = "dbg",
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_dbg_flags(compiler),
                    ),
                ],
            ),
        ],
    )

    # Warning flags
    warnings_feature = feature(
        name = "warnings",
        enabled = True,
        flag_sets = [
            flag_set(
                actions = [
                    ACTION_NAMES.c_compile,
                    ACTION_NAMES.cpp_compile,
                ],
                flag_groups = [
                    flag_group(
                        flags = _get_warning_flags(compiler),
                    ),
                ],
            ),
        ],
    )

    features = [
        cpp_standard_feature,
        default_compile_flags_feature,
        default_link_flags_feature,
        opt_feature,
        dbg_feature,
        warnings_feature,
    ]

    return cc_common.create_cc_toolchain_config_info(
        ctx = ctx,
        features = features,
        toolchain_identifier = ctx.attr.toolchain_identifier,
        host_system_name = ctx.attr.host_system_name,
        target_system_name = ctx.attr.target_system_name,
        target_cpu = cpu,
        target_libc = ctx.attr.target_libc,
        compiler = compiler,
        abi_version = "unknown",
        abi_libc_version = "unknown",
        tool_paths = tool_paths,
        cxx_builtin_include_directories = ctx.attr.cxx_builtin_include_directories,
    )

def _get_cpp_standard_flags(compiler):
    """Get C++ standard flags based on compiler."""
    if compiler == "msvc" or compiler == "clang-cl":
        return ["/std:c++latest"]  # MSVC C++23/26
    else:
        # Try C++26 first, fallback to C++23
        return ["-std=c++26"]  # GCC 14+, Clang 17+ support c++26

def _get_compile_flags(compiler, cpu):
    """Get compiler-specific compilation flags."""
    flags = []
    
    if compiler == "msvc":
        flags.extend([
            "/DWIN32",
            "/D_WINDOWS",
            "/EHsc",  # Exception handling
            "/MD",    # Multithreaded DLL runtime
            "/nologo",
            "/bigobj",
        ])
    elif compiler == "clang-cl":
        flags.extend([
            "/DWIN32",
            "/D_WINDOWS",
            "/EHsc",
            "/MD",
            "-Wno-unused-command-line-argument",
        ])
    else:  # GCC or Clang on Unix
        flags.extend([
            "-fPIC",
            "-fno-omit-frame-pointer",
        ])
        if cpu == "x86_64":
            flags.append("-m64")
        elif cpu == "arm64":
            flags.append("-march=armv8-a")
    
    return flags

def _get_link_flags(compiler, cpu):
    """Get linker flags."""
    flags = []
    
    if compiler == "msvc" or compiler == "clang-cl":
        flags.extend([
            "/MACHINE:X64",
            "/SUBSYSTEM:CONSOLE",
        ])
    else:
        if cpu == "x86_64":
            flags.append("-m64")
    
    return flags

def _get_opt_flags(compiler):
    """Get optimization flags."""
    if compiler == "msvc" or compiler == "clang-cl":
        return [
            "/O2",
            "/DNDEBUG",
        ]
    else:
        return [
            "-O3",
            "-DNDEBUG",
        ]

def _get_dbg_flags(compiler):
    """Get debug flags."""
    if compiler == "msvc" or compiler == "clang-cl":
        return [
            "/Od",
            "/Zi",
            "/DEBUG",
        ]
    else:
        return [
            "-g",
            "-O0",
        ]

def _get_warning_flags(compiler):
    """Get warning flags."""
    if compiler == "msvc":
        return [
            "/W3",
        ]
    elif compiler == "clang-cl":
        return [
            "/W3",
            "-Wno-unused-command-line-argument",
        ]
    else:
        return [
            "-Wall",
            "-Wextra",
        ]

cc_toolchain_config = rule(
    implementation = _impl,
    attrs = {
        "compiler": attr.string(mandatory = True),
        "cpu": attr.string(mandatory = True),
        "cxx_builtin_include_directories": attr.string_list(),
        "host_system_name": attr.string(mandatory = True),
        "target_libc": attr.string(mandatory = True),
        "target_system_name": attr.string(mandatory = True),
        "tool_paths": attr.string_dict(),
        "toolchain_identifier": attr.string(),
    },
    provides = [CcToolchainConfigInfo],
)