"""Helper functions to get compiler paths from detected compilers."""

load("@detected_compilers//:compilers.bzl", "DETECTED_COMPILERS")

def get_msvc_paths():
    """Get MSVC tool paths from detected compilers."""
    msvc = DETECTED_COMPILERS.get("msvc", None)
    winsdk = DETECTED_COMPILERS.get("windows_sdk", None)
    
    if not msvc:
        # Fallback to default paths
        return {
            "base_path": "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807",
            "sdk_path": "C:/Program Files (x86)/Windows Kits/10",
            "sdk_version": "10.0.22621.0",
            "include_dirs": [
                "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.40.33807/include",
                "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt",
                "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um",
                "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared",
            ],
        }
    
    sdk_version = winsdk.get("sdk_version", "10.0.22621.0") if winsdk else "10.0.22621.0"
    sdk_path = winsdk.get("sdk_path", "C:/Program Files (x86)/Windows Kits/10") if winsdk else "C:/Program Files (x86)/Windows Kits/10"
    
    include_dirs = [
        msvc["msvc_path"] + "/include",
        sdk_path + "/Include/" + sdk_version + "/ucrt",
        sdk_path + "/Include/" + sdk_version + "/um",
        sdk_path + "/Include/" + sdk_version + "/shared",
    ]
    
    return {
        "base_path": msvc["msvc_path"],
        "sdk_path": sdk_path,
        "sdk_version": sdk_version,
        "include_dirs": include_dirs,
    }

def get_msvc_tool_paths():
    """Get MSVC tool paths."""
    paths = get_msvc_paths()
    base = paths["base_path"]
    
    return {
        "ar": base + "/bin/Hostx64/x64/lib.exe",
        "cpp": base + "/bin/Hostx64/x64/cl.exe",
        "gcc": base + "/bin/Hostx64/x64/cl.exe",
        "gcov": base + "/bin/Hostx64/x64/cl.exe",
        "ld": base + "/bin/Hostx64/x64/link.exe",
        "nm": base + "/bin/Hostx64/x64/dumpbin.exe",
        "objdump": base + "/bin/Hostx64/x64/dumpbin.exe",
        "strip": base + "/bin/Hostx64/x64/cl.exe",
    }

def get_llvm_windows_paths():
    """Get LLVM/Clang paths for Windows."""
    llvm = DETECTED_COMPILERS.get("llvm_windows", None)
    
    if not llvm:
        # Fallback to default paths
        return {
            "base_path": "C:/Program Files/LLVM",
            "version": "18",
            "include_dirs": [
                "C:/Program Files/LLVM/lib/clang/18/include",
            ],
        }
    
    version = llvm.get("llvm_version", "18")
    base_path = llvm["llvm_path"]
    
    include_dirs = [
        base_path + "/lib/clang/" + version + "/include",
    ]
    
    return {
        "base_path": base_path,
        "version": version,
        "include_dirs": include_dirs,
    }

def get_clang_windows_tool_paths():
    """Get Clang-cl tool paths for Windows."""
    paths = get_llvm_windows_paths()
    base = paths["base_path"]
    
    return {
        "ar": base + "/bin/llvm-ar.exe",
        "cpp": base + "/bin/clang-cl.exe",
        "gcc": base + "/bin/clang-cl.exe",
        "gcov": base + "/bin/llvm-cov.exe",
        "ld": base + "/bin/lld-link.exe",
        "nm": base + "/bin/llvm-nm.exe",
        "objdump": base + "/bin/llvm-objdump.exe",
        "strip": base + "/bin/llvm-strip.exe",
    }

def get_clang_windows_include_dirs():
    """Get include directories for Clang on Windows."""
    llvm_paths = get_llvm_windows_paths()
    msvc_paths = get_msvc_paths()
    
    include_dirs = llvm_paths["include_dirs"] + msvc_paths["include_dirs"]
    return include_dirs

def get_clang_linux_paths():
    """Get Clang paths for Linux."""
    clang = DETECTED_COMPILERS.get("clang_linux", None)
    
    if not clang:
        # Fallback to default paths
        return {
            "clang_path": "/usr/bin/clang",
            "include_dirs": [
                "/usr/lib/clang/18/include",
                "/usr/lib/clang/17/include",
                "/usr/lib/clang/16/include",
                "/usr/include",
            ],
        }
    
    return {
        "clang_path": clang["clang_path"],
        "include_dirs": clang.get("include_dirs", ["/usr/include"]),
    }

def get_clang_linux_tool_paths():
    """Get Clang tool paths for Linux."""
    return {
        "ar": "/usr/bin/ar",
        "cpp": "/usr/bin/clang-cpp",
        "gcc": "/usr/bin/clang",
        "gcov": "/usr/bin/llvm-cov",
        "ld": "/usr/bin/ld.lld",
        "nm": "/usr/bin/llvm-nm",
        "objdump": "/usr/bin/llvm-objdump",
        "strip": "/usr/bin/strip",
    }

def get_gcc_linux_paths():
    """Get GCC paths for Linux."""
    gcc = DETECTED_COMPILERS.get("gcc_linux", None)
    
    if not gcc:
        # Fallback to default paths
        return {
            "gcc_path": "/usr/bin/gcc",
            "include_dirs": [
                "/usr/include/c++/13",
                "/usr/include/c++/12",
                "/usr/include/c++/11",
                "/usr/include",
            ],
        }
    
    return {
        "gcc_path": gcc["gcc_path"],
        "include_dirs": gcc.get("include_dirs", ["/usr/include"]),
    }

def get_gcc_linux_tool_paths():
    """Get GCC tool paths for Linux."""
    return {
        "ar": "/usr/bin/ar",
        "cpp": "/usr/bin/cpp",
        "gcc": "/usr/bin/gcc",
        "gcov": "/usr/bin/gcov",
        "ld": "/usr/bin/ld",
        "nm": "/usr/bin/nm",
        "objdump": "/usr/bin/objdump",
        "strip": "/usr/bin/strip",
    }