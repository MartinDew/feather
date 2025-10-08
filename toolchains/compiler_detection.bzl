"""Auto-detection of compiler installations."""

def _find_msvc_installation(repository_ctx):
    """Find MSVC installation on Windows."""
    
    # Common Visual Studio installation paths
    vs_paths = [
        "C:/Program Files/Microsoft Visual Studio/2022/Community",
        "C:/Program Files/Microsoft Visual Studio/2022/Professional",
        "C:/Program Files/Microsoft Visual Studio/2022/Enterprise",
        "C:/Program Files/Microsoft Visual Studio/2022/BuildTools",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/Community",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/Professional",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/Enterprise",
        "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools",
        "C:/Program Files/Microsoft Visual Studio/2019/Community",
        "C:/Program Files/Microsoft Visual Studio/2019/Professional",
        "C:/Program Files/Microsoft Visual Studio/2019/Enterprise",
        "C:/Program Files/Microsoft Visual Studio/2019/BuildTools",
    ]
    
    # Try to use vswhere if available
    vswhere_path = "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
    if repository_ctx.path(vswhere_path).exists:
        result = repository_ctx.execute([
            vswhere_path,
            "-latest",
            "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath",
        ])
        if result.return_code == 0 and result.stdout.strip():
            vs_paths.insert(0, result.stdout.strip())
    
    # Search for valid installation
    for vs_path in vs_paths:
        msvc_base = vs_path + "/VC/Tools/MSVC"
        if repository_ctx.path(msvc_base).exists:
            # Find the highest version number
            result = repository_ctx.execute(["cmd.exe", "/c", "dir", "/b", msvc_base])
            if result.return_code == 0:
                versions = [v.strip() for v in result.stdout.split("\n") if v.strip()]
                if versions:
                    # Sort versions and get the latest
                    versions.sort(reverse=True)
                    msvc_version = versions[0]
                    msvc_path = msvc_base + "/" + msvc_version
                    
                    # Verify cl.exe exists
                    cl_path = msvc_path + "/bin/Hostx64/x64/cl.exe"
                    if repository_ctx.path(cl_path).exists:
                        return {
                            "msvc_path": msvc_path,
                            "msvc_version": msvc_version,
                            "vs_path": vs_path,
                        }
    
    return None

def _find_windows_sdk(repository_ctx):
    """Find Windows SDK installation."""
    
    sdk_base_paths = [
        "C:/Program Files (x86)/Windows Kits/10",
        "C:/Program Files/Windows Kits/10",
    ]
    
    for sdk_base in sdk_base_paths:
        include_path = sdk_base + "/Include"
        if repository_ctx.path(include_path).exists:
            # Find the highest SDK version
            result = repository_ctx.execute(["cmd.exe", "/c", "dir", "/b", include_path])
            if result.return_code == 0:
                versions = [v.strip() for v in result.stdout.split("\n") if v.strip()]
                if versions:
                    versions.sort(reverse=True)
                    sdk_version = versions[0]
                    ucrt_path = include_path + "/" + sdk_version + "/ucrt"
                    if repository_ctx.path(ucrt_path).exists:
                        return {
                            "sdk_path": sdk_base,
                            "sdk_version": sdk_version,
                        }
    
    return None

def _find_llvm_windows(repository_ctx):
    """Find LLVM/Clang installation on Windows."""
    
    llvm_paths = [
        "C:/Program Files/LLVM",
        "C:/Program Files (x86)/LLVM",
        "C:/LLVM",
    ]
    
    # Try to find in PATH
    result = repository_ctx.execute(["where", "clang-cl.exe"])
    if result.return_code == 0:
        clang_cl_path = result.stdout.strip().split("\n")[0]
        # Get the bin directory, then parent directory
        llvm_path = repository_ctx.path(clang_cl_path).dirname.dirname
        llvm_paths.insert(0, str(llvm_path))
    
    for llvm_path in llvm_paths:
        clang_cl = llvm_path + "/bin/clang-cl.exe"
        if repository_ctx.path(clang_cl).exists:
            # Detect LLVM version
            result = repository_ctx.execute([clang_cl, "--version"])
            version = "unknown"
            if result.return_code == 0:
                lines = result.stdout.split("\n")
                if lines:
                    # Parse version from first line
                    parts = lines[0].split()
                    for i, part in enumerate(parts):
                        if part == "version" and i + 1 < len(parts):
                            version = parts[i + 1].split(".")[0]  # Major version
                            break
            
            return {
                "llvm_path": llvm_path,
                "llvm_version": version,
            }
    
    return None

def _find_clang_linux(repository_ctx):
    """Find Clang installation on Linux."""
    
    # Try to find in PATH
    result = repository_ctx.execute(["which", "clang"])
    if result.return_code == 0:
        clang_path = result.stdout.strip()
        
        # Get version
        result = repository_ctx.execute(["clang", "--version"])
        version = "unknown"
        if result.return_code == 0:
            lines = result.stdout.split("\n")
            if lines:
                parts = lines[0].split()
                for i, part in enumerate(parts):
                    if part == "version" and i + 1 < len(parts):
                        version = parts[i + 1].split(".")[0]
                        break
        
        # Find include directories
        result = repository_ctx.execute(["clang", "-print-resource-dir"])
        include_dirs = []
        if result.return_code == 0:
            resource_dir = result.stdout.strip()
            include_dirs.append(resource_dir + "/include")
        
        # Add common system include paths
        include_dirs.extend([
            "/usr/include",
            "/usr/local/include",
        ])
        
        return {
            "clang_path": clang_path,
            "clang_version": version,
            "include_dirs": include_dirs,
        }
    
    return None

def _find_gcc_linux(repository_ctx):
    """Find GCC installation on Linux."""
    
    result = repository_ctx.execute(["which", "gcc"])
    if result.return_code == 0:
        gcc_path = result.stdout.strip()
        
        # Get version
        result = repository_ctx.execute(["gcc", "-dumpversion"])
        version = "unknown"
        if result.return_code == 0:
            version = result.stdout.strip().split(".")[0]
        
        # Find include directories
        result = repository_ctx.execute([
            "gcc",
            "-print-prog-name=cc1plus"
        ])
        
        include_dirs = []
        if result.return_code == 0:
            # Get C++ include paths
            result = repository_ctx.execute([
                "gcc",
                "-E", "-x", "c++", "-", "-v"
            ], stdin_content = "")
            
            if result.return_code == 0 or result.stderr:
                output = result.stderr
                in_include_section = False
                for line in output.split("\n"):
                    if "#include <...> search starts here:" in line:
                        in_include_section = True
                        continue
                    if in_include_section:
                        if line.startswith("End of search list"):
                            break
                        path = line.strip()
                        if path and not path.startswith("#"):
                            include_dirs.append(path)
        
        if not include_dirs:
            # Fallback to common paths
            include_dirs = [
                "/usr/include/c++/13",
                "/usr/include/c++/12",
                "/usr/include/c++/11",
                "/usr/include",
            ]
        
        return {
            "gcc_path": gcc_path,
            "gcc_version": version,
            "include_dirs": include_dirs,
        }
    
    return None

def _detect_compilers_impl(repository_ctx):
    """Repository rule to detect compiler installations."""
    
    is_windows = repository_ctx.os.name.startswith("windows")
    is_linux = repository_ctx.os.name.startswith("linux")
    is_macos = repository_ctx.os.name.startswith("mac")
    
    compilers = {}
    
    if is_windows:
        msvc = _find_msvc_installation(repository_ctx)
        if msvc:
            compilers["msvc"] = msvc
        
        winsdk = _find_windows_sdk(repository_ctx)
        if winsdk:
            compilers["windows_sdk"] = winsdk
        
        llvm = _find_llvm_windows(repository_ctx)
        if llvm:
            compilers["llvm_windows"] = llvm
    
    if is_linux:
        clang = _find_clang_linux(repository_ctx)
        if clang:
            compilers["clang_linux"] = clang
        
        gcc = _find_gcc_linux(repository_ctx)
        if gcc:
            compilers["gcc_linux"] = gcc
    
    # Write detection results to a file
    content = """# Auto-generated compiler detection results

DETECTED_COMPILERS = {compilers}

def get_compiler_info(compiler_name):
    \"\"\"Get detected compiler information.\"\"\"
    return DETECTED_COMPILERS.get(compiler_name, None)
""".format(compilers = repr(compilers))
    
    repository_ctx.file("BUILD.bazel", "")
    repository_ctx.file("compilers.bzl", content)
    
    # Print detection results
    print("Detected compilers:")
    for name, info in compilers.items():
        print("  - {}: {}".format(name, info))

detect_compilers = repository_rule(
    implementation = _detect_compilers_impl,
    local = True,
    environ = ["PATH"],
)

def _detect_compilers_extension_impl(module_ctx):
    """Module extension to detect compilers."""
    detect_compilers(name = "detected_compilers")

detect_compilers_extension = module_extension(
    implementation = _detect_compilers_extension_impl,
)