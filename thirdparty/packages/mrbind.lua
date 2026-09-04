-- MRBind (https://github.com/MeshInspector/mrbind): a Clang-based C++ header
-- parser and C/C# binding generator. Provides three host tools -- mrbind,
-- mrbind_gen_c, mrbind_gen_csharp -- no linkable library, so this package
-- just builds and installs the binaries; nothing in the engine links against
-- it (yet).
--
-- Feather's own C++ wrapper generator (tools/SDK/gen_cpp) is grafted into this
-- source tree and installed as a fourth tool, feather_gen_cpp. See
-- _graft_feather_gen_cpp below for why it is built here rather than separately.

-- Where the grafted generator's sources live. Captured as a value while this file loads, not computed inside a callback: os.scriptdir()
-- during on_install resolves against a different script (same pattern as FeatherPluginSDK.lua's SDK_DIR). Two levels up is the engine root, not os.projectdir() (a consumer's project cross-repo).
local FEATHER_GEN_CPP_DIR = path.join(path.directory(path.directory(os.scriptdir())),
    "tools", "SDK", "gen_cpp")

-- Content hash of the grafted generator's sources, passed by the requiring side as a package config -- a package's install hash only follows
-- configs it's asked for, so setting this from inside the package itself would never invalidate it. KEEP IN SYNC with mrbind_generators.lua.
function feather_gen_cpp_rev(dir)
    dir = dir or FEATHER_GEN_CPP_DIR
    if not os.isdir(dir) then
        return ""
    end
    local files = os.files(path.join(dir, "**"))
    table.sort(files)
    local parts = {}
    for _, f in ipairs(files) do
        table.insert(parts, path.relative(f, dir) .. ":" .. hash.sha256(f))
    end
    return hash.strhash128(table.concat(parts, "\0"))
end

package("mrbind")
    set_kind("binary")
    set_homepage("https://github.com/MeshInspector/mrbind")
    set_description("A Clang-based C++ header parser and C/C# binding generator")
    set_license("MIT")

    -- Not a user-facing option: it exists so the install hash follows the
    -- grafted generator's sources (see feather_gen_cpp_rev).
    add_configs("gen_cpp_rev", {description = "Content hash of the vendored feather_gen_cpp sources.", default = "", type = "string"})

    -- Pinned, not tracking master: the published parse output (api.json) has an undocumented, unversioned schema upstream, so a generator
    -- from a different revision may fail to read it. tools/SDK/packages/mrbind_generators.lua pins the same commit; move both together.
    add_urls("https://github.com/MeshInspector/mrbind.git", {commit = "232ff33159d5e76e57b11669453d7d25ad22a14d"})
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

        -- The prebuilt Windows LLVM bakes an absolute, nonexistent DIA SDK path into LLVMDebugInfoPDB, which mrbind links transitively, so
        -- the build dies missing diaguids.lib. Repointed via find_dia_sdk when Visual Studio has one; only dropped (mrbind never reads PDBs) otherwise.
        local function _fix_baked_dia_path(installdir)
            local llvm_exports = path.join(installdir, "lib", "cmake", "llvm", "LLVMExports.cmake")
            if not os.isfile(llvm_exports) then
                return
            end

            local contents = io.readfile(llvm_exports)
            -- The pattern is anchored on the filename, since only the directory varies.
            if not contents:find("diaguids%.lib") then
                -- Already patched, or a future archive fixed it upstream.
                return
            end

            local replacement
            local dia = try { function ()
                import("detect.sdks.find_dia_sdk")
                return find_dia_sdk(nil, {arch = "x64"})
            end }
            if dia and dia.linkdirs then
                for _, dir in ipairs(table.wrap(dia.linkdirs)) do
                    local candidate = path.join(dir, "diaguids.lib")
                    if os.isfile(candidate) then
                        -- CMake wants forward slashes in a path inside a quoted list.
                        replacement = candidate:gsub("\\", "/")
                        break
                    end
                end
            end

            if replacement then
                io.replace(llvm_exports, '[A-Za-z]:[/\\][^";]-diaguids%.lib', replacement, {plain = false})
                cprint("${cyan}[mrbind]${reset} repointed LLVM's baked DIA SDK path at %s", replacement)
            else
                -- Drop the entry and the separator that follows it, leaving the rest of
                -- the INTERFACE_LINK_LIBRARIES list intact.
                io.replace(llvm_exports, '[A-Za-z]:[/\\][^";]-diaguids%.lib;', "", {plain = false})
                io.replace(llvm_exports, ';[A-Za-z]:[/\\][^";]-diaguids%.lib', "", {plain = false})
                cprint("${yellow}[mrbind]${reset} no DIA SDK found; removed LLVM's baked reference to it."
                    .. " If linking now fails on DIA symbols, install the Visual Studio 'Debugging Tools' component.")
            end

            if io.readfile(llvm_exports):find("diaguids%.lib") and not replacement then
                raise("mrbind: failed to remove the baked DIA SDK path from " .. llvm_exports)
            end
        end

        local llvm_config = package:data("llvm_config")
        local static_build = llvm_config == nil

        local cc, cxx, clang_dir, llvm_dir
        if llvm_config then

            local suffix = package:data("llvm_suffix") or ""
            local bindir = path.directory(llvm_config)
            cc  = path.join(bindir, "clang" .. suffix)
            cxx = path.join(bindir, "clang++" .. suffix)

            -- mrbind's find_package(Clang) needs Clang's CMake config (which does its own find_package(LLVM)); both need pointing at
            -- explicitly since a versioned install puts neither on CMake's default search path. Asking llvm-config beats guessing the layout from the version suffix.
            llvm_dir = try { function () return os.iorunv(llvm_config, {"--cmakedir"}):trim() end }
            if llvm_dir and os.isdir(llvm_dir) then
                -- Clang installs alongside LLVM, as a sibling of its cmake dir.
                local candidate = path.join(path.directory(llvm_dir), "clang")
                if os.isdir(candidate) then
                    clang_dir = candidate
                end
            else
                llvm_dir = nil
            end
        else
            local libllvm = package:dep("libllvm")
            local installdir = libllvm:installdir()
            local bindir = path.join(installdir, "bin")
            cc  = path.join(bindir, "clang")
            cxx = path.join(bindir, "clang++")
            clang_dir = path.join(installdir, "lib", "cmake", "clang")
            llvm_dir = path.join(installdir, "lib", "cmake", "llvm")

            if package:is_plat("windows") then
                cc  = cc  .. ".exe"
                cxx = cxx .. ".exe"

                _fix_baked_dia_path(installdir)
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
        if llvm_dir then
            table.insert(configs, "-DLLVM_DIR=" .. llvm_dir)
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

        -- Lets --expose-as-struct accept standard-layout classes with base classes -- what SimpleMath's Vector2/3/4, Quaternion and Color are,
        -- fields inherited from XMFLOAT2/3/4. Size/alignment/offset validation is untouched. Rationale: tools/SDK/gen_cpp/patches/expose-as-struct-standard-layout-bases.md. KEEP IN SYNC with mrbind_generators.lua.
        local function _allow_exposed_structs_with_bases()
            local f = path.join("src", "generators", "c", "generator.cpp")
            local needle = '                // Must have no bases. I ain\'t dealing with those.\n'
                .. '                if (!class_info.parsed->bases.empty())\n'
                .. '                    throw std::runtime_error("The class `" + cpp_type_name + "` is whitelisted by `--expose-as-struct`, but it has a base class. This flag only supports the structs/classes with no base classes.");\n'

            local contents = io.readfile(f)
            if contents:find(needle, 1, true) then
                -- Parenthesized: replace() also returns a count, which would
                -- otherwise land in writefile's opt parameter.
                io.writefile(f, (contents:replace(needle, "", {plain = true})))
            end
            -- An upstream edit to this text must fail the build rather than
            -- silently leave the check in and break the math bindings.
            assert(not io.readfile(f):find("I ain't dealing with those", 1, true),
                "mrbind: could not remove the --expose-as-struct no-bases check from " .. f
                .. " -- upstream source moved; see tools/SDK/gen_cpp/patches/")
        end

        -- Copies Feather's C++ wrapper generator into the fetched source tree and hooks it into mrbind's own CMakeLists. Grafted rather than
        -- built standalone: mrbind sets -std=c++23/_ITERATOR_DEBUG_LEVEL=0/CMAKE_MSVC_RUNTIME_LIBRARY at directory scope; missing any fails to link (LNK2038). KEEP IN SYNC with mrbind_generators.lua.
        local function _graft_feather_gen_cpp()
            local src = FEATHER_GEN_CPP_DIR
            assert(os.isdir(src), "mrbind: feather_gen_cpp sources not found at " .. src)
            os.tryrm("feather_gen_cpp")
            os.cp(src, "feather_gen_cpp")

            -- Explicit binary dir: the executable goes to the build root, so a
            -- build folder named after it would be the linker's output path.
            local line = "add_subdirectory(feather_gen_cpp _feather_gen_cpp_build)"
            local contents = io.readfile("CMakeLists.txt")
            if not contents:find(line, 1, true) then
                io.writefile("CMakeLists.txt", contents:rtrim() .. "\n" .. line .. "\n")
            end
        end

        _allow_exposed_structs_with_bases()
        _graft_feather_gen_cpp()

        local builddir = path.join(package:builddir(), ".cmake_build")
        cmake.build(package, configs, {builddir = builddir, cmake_generator = "Ninja"})

        -- No install() rules upstream: copy the tool binaries out of the build tree by hand. Checks both .exe spellings rather than trusting
        -- is_plat("windows") alone -- it was observed disagreeing with os.host() on Windows CI for this host=true package's on_install.
        local bindir = package:installdir("bin")
        for _, name in ipairs({"mrbind", "mrbind_gen_c", "mrbind_gen_csharp", "feather_gen_cpp"}) do
            local preferred = os.host() == "windows" and (name .. ".exe") or name
            local fallback = os.host() == "windows" and name or (name .. ".exe")
            local built = path.join(builddir, preferred)
            if not os.isfile(built) then
                built = path.join(builddir, fallback)
            end
            assert(os.isfile(built), "mrbind: expected build output missing: "
                .. path.join(builddir, preferred) .. " (also checked " .. fallback .. ")")
            os.cp(built, bindir)
        end

        -- The Python backend has no generator binary: the parser emits macros (--format=macros) compiled against mrbind's own target
        -- header, so the source tree's include/ has to ship too (modules/py_bindings, mrbind's docs/generating_python.md).
        assert(os.isdir("include"), "mrbind: source tree has no include/ -- upstream layout changed")
        os.cp("include", package:installdir())
    end)

    on_test(function (package)
        os.vrun(path.join(package:installdir("bin"), "mrbind" .. (os.host() == "windows" and ".exe" or "")) .. " --help")
    end)
package_end()
