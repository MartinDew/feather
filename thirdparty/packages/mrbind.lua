-- MRBind (https://github.com/MeshInspector/mrbind): a Clang-based C++ header
-- parser and C/C# binding generator. Provides three host tools -- mrbind,
-- mrbind_gen_c, mrbind_gen_csharp -- no linkable library, so this package
-- just builds and installs the binaries; nothing in the engine links against
-- it (yet).
package("mrbind")
    set_kind("binary")
    set_homepage("https://github.com/MeshInspector/mrbind")
    set_description("A Clang-based C++ header parser and C/C# binding generator")
    set_license("MIT")

    add_urls("https://github.com/MeshInspector/mrbind.git", {branch = "master"})
    set_policy("platform.longpaths", true)

    add_deps("cmake", "ninja")

    on_load(function (package)
        import("lib.detect.find_tool")

        local llvm_config, suffix
        if not package:is_plat("windows") then
            llvm_config = find_tool("llvm-config", {
                -- Homebrew's llvm is keg-only (not linked onto PATH by
                -- default) precisely to avoid clobbering macOS's own clang.
                paths = {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"},
            })
            suffix = ""
            if not llvm_config then
                for v = 30, 18, -1 do
                    local candidate = find_tool("llvm-config", {program = "llvm-config-" .. v})
                    if candidate then
                        llvm_config = candidate
                        suffix = "-" .. v
                        break
                    end
                end
            end
        end

        if llvm_config then
            package:data_set("llvm_config", llvm_config.program)
            package:data_set("llvm_suffix", suffix)
        else
            package:add("deps", "libllvm", {configs = {shared = false, clang = true}})
        end
    end)

    on_install(function (package)
        import("package.tools.cmake")

        local llvm_config = package:data("llvm_config")
        local static_build = llvm_config == nil

        local cc, cxx, clang_dir
        if llvm_config then

            local suffix = package:data("llvm_suffix") or ""
            local bindir = path.directory(llvm_config)
            cc  = path.join(bindir, "clang" .. suffix)
            cxx = path.join(bindir, "clang++" .. suffix)

            if suffix ~= "" and package:is_plat("linux") then
                clang_dir = "/usr/lib/cmake/clang" .. suffix
            end
        else
            local libllvm = package:dep("libllvm")
            local installdir = libllvm:installdir()
            local bindir = path.join(installdir, "bin")
            cc  = path.join(bindir, "clang")
            cxx = path.join(bindir, "clang++")
            clang_dir = path.join(installdir, "lib", "cmake", "clang")

            if package:is_plat("windows") then
                cc  = cc  .. ".exe"
                cxx = cxx .. ".exe"

                local llvm_exports = path.join(installdir, "lib", "cmake", "llvm", "LLVMExports.cmake")
                if os.isfile(llvm_exports) then
                    local before = io.readfile(llvm_exports)
                    io.replace(llvm_exports,
                        '[A-Za-z]:/[^";]-diaguids%.lib',
                        "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64/diaguids.lib")
                    if io.readfile(llvm_exports) == before then
                        cprint("${yellow}[mrbind]${reset} no baked DIA SDK path found to patch in %s"
                            .. " -- upstream may have changed; re-check docs/building_mrbind.md", llvm_exports)
                    end
                end
            end
        end

        local configs = {
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "RelWithDebInfo"),
            "-DCMAKE_C_COMPILER=" .. cc,
            "-DCMAKE_CXX_COMPILER=" .. cxx,
            "-DMRBIND_STATIC_BUILD=" .. (static_build and "ON" or "OFF"),
            "-DMRBIND_BUILD_GENERATOR_C=ON",
            "-DMRBIND_BUILD_GENERATOR_CSHARP=ON",
        }
        if clang_dir then
            table.insert(configs, "-DClang_DIR=" .. clang_dir)
        end

        if package:is_plat("windows") then
            local buildtype_map = {debug = "DEBUG", release = "RELEASE", releasedbg = "RELWITHDEBINFO"}
            local buildtype = buildtype_map[package:mode()] or "RELEASE"
            for _, key in ipairs({
                "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_" .. buildtype,
                "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_" .. buildtype,
                "CMAKE_EXE_LINKER_FLAGS_" .. buildtype,
                "CMAKE_SHARED_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS_" .. buildtype,
                "CMAKE_STATIC_LINKER_FLAGS", "CMAKE_STATIC_LINKER_FLAGS_" .. buildtype,
            }) do
                table.insert(configs, "-D" .. key .. "=")
            end
        end

        local builddir = path.join(package:builddir(), ".cmake_build")
        cmake.build(package, configs, {builddir = builddir, cmake_generator = "Ninja"})

        -- No install() rules upstream: copy the three tool binaries out of
        -- the build tree by hand.
        local bindir = package:installdir("bin")
        for _, name in ipairs({"mrbind", "mrbind_gen_c", "mrbind_gen_csharp"}) do
            local built = path.join(builddir, package:is_plat("windows") and (name .. ".exe") or name)
            assert(os.isfile(built), "mrbind: expected build output missing: " .. built)
            os.cp(built, bindir)
        end
    end)

    on_test(function (package)
        os.vrun(path.join(package:installdir("bin"), "mrbind" .. (package:is_plat("windows") and ".exe" or "")) .. " --help")
    end)
package_end()
