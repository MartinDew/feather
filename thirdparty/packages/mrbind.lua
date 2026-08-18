-- MRBind (https://github.com/MeshInspector/mrbind): a Clang-based C++ header
-- parser and C/C# binding generator. Provides three host tools -- mrbind,
-- mrbind_gen_c, mrbind_gen_csharp -- no linkable library, so this package
-- just builds and installs the binaries; nothing in the engine links against
-- it (yet).
--
-- mrbind has no cmake install() rules at all (upstream CMakeLists.txt just
-- add_executable()s the tools), so on_install builds in-tree and copies the
-- binaries out by hand.
--
-- Clang/LLVM: mrbind's CMakeLists does find_package(Clang REQUIRED) and
-- needs Clang 18+ dev libraries. Two paths, matching upstream's own
-- docs/building_mrbind.md:
--   * System Clang/LLVM already installed (Linux via llvm-config on PATH,
--     e.g. Arch's `clang`+`llvm` packages, or Debian/Ubuntu's versioned
--     llvm-config-N when there's no bare symlink; macOS via Homebrew's
--     `llvm` keg, which isn't linked onto PATH by default so its bin dir
--     is probed explicitly): mrbind's CMakeLists links the large merged
--     dylibs (`LLVM`, `clang-cpp`) in this mode, so MRBIND_STATIC_BUILD
--     must stay OFF.
--   * No system Clang/LLVM (always the case on Windows -- see below): fall
--     back to xrepo's "libllvm" package, built with `clang = true`. That
--     package only produces fine-grained static libs (no merged dylib
--     targets), so MRBIND_STATIC_BUILD=ON is required there to link
--     mrbind against the `clangTooling` static target instead.
package("mrbind")
    set_kind("binary")
    set_homepage("https://github.com/MeshInspector/mrbind")
    set_description("A Clang-based C++ header parser and C/C# binding generator")
    set_license("MIT")

    add_urls("https://github.com/MeshInspector/mrbind.git", {branch = "master"})

    -- Ninja explicitly (per building_mrbind.md's own recommended invocation):
    -- the cmake module's default generator is per-config Visual Studio on
    -- Windows, which would put binaries under builddir/<Config>/ instead of
    -- flat in builddir, complicating the copy-out step below for no benefit.
    add_deps("cmake", "ninja")

    on_load(function (package)
        import("lib.detect.find_tool")

        -- Windows: skip the system-detection fast path unconditionally.
        -- MRBind's own docs/building_mrbind.md documents that Windows Clang
        -- distributions (whichever one is found -- a locally installed one,
        -- or the xrepo-mirrored official release zip pulled in below) bake
        -- an absolute, machine-specific DIA SDK path into
        -- lib/cmake/llvm/LLVMExports.cmake's LLVMDebugInfoPDB target, which
        -- breaks linking mrbind.exe on any machine other than the one that
        -- built that particular Clang distribution. on_install patches that
        -- baked path (also per building_mrbind.md) once libllvm is a known,
        -- fixed dependency -- which needs the fast path skipped here so
        -- that dependency reliably exists to patch.
        local llvm_config, suffix
        if not package:is_plat("windows") then
            llvm_config = find_tool("llvm-config", {
                -- Homebrew's llvm is keg-only (not linked onto PATH by
                -- default) precisely to avoid clobbering macOS's own clang.
                paths = {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"},
            })
            suffix = ""
            if not llvm_config then
                -- Debian/Ubuntu ship only versioned llvm-config-N (no bare
                -- "llvm-config" symlink) -- search newest-first for one
                -- meeting mrbind's Clang 18+ floor, rather than silently
                -- falling through to the much slower libllvm-from-source
                -- path below with a perfectly good system Clang sitting
                -- right there unversioned-only find_tool can't see.
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
            -- System path: derive clang/clang++ from llvm-config's own bin
            -- dir (same suffix, if any -- Debian/Ubuntu's clang-tools-N
            -- package installs "clang-N"/"clang++-N" alongside
            -- "llvm-config-N", all unsuffixed on Arch/Homebrew), so we use
            -- the matching compiler, not whatever xmake's resolved
            -- toolchain happens to be (per building_mrbind.md: "Use the
            -- same Clang compiler that provides the parsing libraries").
            local suffix = package:data("llvm_suffix") or ""
            local bindir = path.directory(llvm_config)
            cc  = path.join(bindir, "clang" .. suffix)
            cxx = path.join(bindir, "clang++" .. suffix)

            -- Ubuntu's docs/building_mrbind.md warns CMake can find the
            -- wrong Clang_DIR when multiple versions coexist (common on
            -- GitHub-hosted runner images) -- pin it explicitly using the
            -- same versioned path its own Ubuntu instructions use. Bare
            -- "llvm-config" (Arch/Homebrew) means a single unversioned
            -- install, where CMake's default search is unambiguous, same
            -- as upstream's own Arch instructions.
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

                -- Patch the baked absolute DIA SDK path (see on_load above
                -- and building_mrbind.md's "Common build issues" section,
                -- which documents this exact fix) so it resolves relative
                -- to whatever machine actually runs this build, instead of
                -- whichever machine built the xrepo-mirrored LLVM zip.
                -- $ENV{VSINSTALLDIR} is set by the VS dev environment that
                -- xmake's msvc toolchain already runs cmake/ninja under.
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
            -- xmake's package.tools.cmake helper probes CMake's own
            -- "default" C/CXX/linker/archiver flags by test-configuring a
            -- throwaway project against the PROJECT's own resolved compiler
            -- (package:build_getenv("cc"), i.e. this repo's --toolchain=
            -- clang-cl on Windows) -- entirely independent of the
            -- CMAKE_C_COMPILER override above, which deliberately points at
            -- a different, plain (non-clang-cl) clang.exe. It then force-
            -- feeds those probed flags into the real build regardless. That
            -- silently "works" on Linux (GCC's and Clang's default flags
            -- are both -D-style), but on Windows it hands clang-cl's
            -- MSVC-style defaults ("/DWIN32 /D_WINDOWS" for the compiler,
            -- "/machine:x64" for the archiver/linker) to plain clang.exe's
            -- toolchain (clang.exe itself, plus LLVM's llvm-ar), neither of
            -- which understands slash-style MSVC flags -- first breaking
            -- the compiler self-test, then (once that's suppressed) the
            -- static-archive step, then (next, if not suppressed too) the
            -- final exe link. Pre-declaring every key
            -- cmake.lua's _get_default_flags() probes makes xmake's
            -- "already user-specified" check skip its own (wrong)
            -- injection for all of them at once; mrbind's own CMakeLists
            -- appends everything it actually needs on top regardless of
            -- these starting empty.
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
