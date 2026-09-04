-- MRBind's binding generators, without its parser.
--
-- This is the plugin-side counterpart to thirdparty/packages/mrbind.lua. A
-- plugin project never parses C++: the engine did that once and shipped the
-- result as feather_api.json, and turning that JSON into C headers or C#
-- sources is all a plugin build has to do.
--
-- That distinction is what makes this package cheap. mrbind_gen_c and
-- mrbind_gen_csharp link no Clang and no LLVM -- only the parser does -- so
-- with MRBIND_BUILD_PARSER=OFF this builds with a plain host compiler in a
-- couple of minutes, instead of potentially building LLVM from source. It also
-- means a plugin author needs no Clang installation, and no engine checkout.
--
-- Keep the git revision in sync with thirdparty/packages/mrbind.lua: the
-- parser's JSON schema is undocumented and unversioned upstream, so a
-- generator from a different revision may not read the engine's api.json.
--
-- Feather's own C++ wrapper generator (../feather_cpp/gen_cpp) is grafted into this source
-- tree and installed as a third tool, feather_gen_cpp. See
-- _graft_feather_gen_cpp below for why it is built here rather than separately.

-- Where the grafted generator's sources live. Captured as a value while this file loads, not computed inside a callback: os.scriptdir()
-- during on_install resolves against a different script (same pattern as FeatherPluginSDK.lua's SDK_DIR). One level up is the SDK root, not os.projectdir() (the vendoring plugin project's own directory).
local FEATHER_GEN_CPP_DIR = path.join(path.directory(os.scriptdir()), "feather_cpp", "gen_cpp")

-- Content hash of the grafted generator's sources, passed by the requiring side as a package config -- a package's install hash only follows
-- configs it's asked for, so setting this from inside the package itself would never invalidate it. KEEP IN SYNC with thirdparty/packages/mrbind.lua.
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

package("mrbind_generators")
    set_kind("binary")
    set_homepage("https://github.com/MeshInspector/mrbind")
    set_description("MRBind's C and C# binding generators (no parser, no LLVM)")
    set_license("MIT")

    -- KEEP IN SYNC with thirdparty/packages/mrbind.lua's pin: a generator has
    -- to be able to read the api.json the engine's parser produced.
    add_urls("https://github.com/MeshInspector/mrbind.git", {commit = "232ff33159d5e76e57b11669453d7d25ad22a14d"})
    set_policy("platform.longpaths", true)

    add_deps("cmake", "ninja")

    -- Not a user-facing option: it exists so the install hash follows the
    -- grafted generator's sources (see feather_gen_cpp_rev).
    add_configs("gen_cpp_rev", {description = "Content hash of the vendored feather_gen_cpp sources.", default = "", type = "string"})

    on_install(function (package)
        import("package.tools.cmake")
        import("lib.detect.find_tool")

        local configs = {
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "RelWithDebInfo"),
            -- The whole point of this package: no parser, so no
            -- find_package(Clang), so no LLVM anywhere in the build.
            "-DMRBIND_BUILD_PARSER=OFF",
            "-DMRBIND_BUILD_GENERATOR_C=ON",
            "-DMRBIND_BUILD_GENERATOR_CSHARP=ON",
        }

        -- Real cl.exe rejects mrbind's C++23 auto(x) decay-copy (src/common/strings.h) on every MSVC toolset through VS 2022 -- missing
        -- until VS 2026, not a flags problem. clang-cl takes the same MSVC-style flags but supports auto(x), so it's preferred when found; with neither, CMake falls back to plain clang++, which needs none of this (and would reject these MSVC-syntax flags outright).
        local compiler = os.host() == "windows" and (find_tool("clang-cl") or find_tool("cl")) or nil
        if compiler then
            table.insert(configs, "-DCMAKE_C_COMPILER=" .. compiler.program)
            table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. compiler.program)

            -- mrbind's CMakeLists only requests a C++ standard `if(NOT MSVC)`, true for clang-cl too, so left unset it silently compiles
            -- pre-C++17 and <filesystem> fails to find std::filesystem. CMAKE_CXX_STANDARD, not /std: -- CMakeLists never sets it itself (commented out), so the external variable takes effect unshadowed, unlike CMAKE_CXX_FLAGS below.
            table.insert(configs, "-DCMAKE_CXX_STANDARD=23")
            table.insert(configs, "-DCMAKE_CXX_STANDARD_REQUIRED=ON")

            local buildtype_map = {debug = "DEBUG", release = "RELEASE", releasedbg = "RELWITHDEBINFO"}
            local buildtype = buildtype_map[package:mode()] or "RELEASE"
            for _, key in ipairs({
                "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_" .. buildtype,
                "CMAKE_CXX_FLAGS_" .. buildtype,
                "CMAKE_EXE_LINKER_FLAGS_" .. buildtype,
            }) do
                table.insert(configs, "-D" .. key .. "=")
            end

            -- /Zc:preprocessor: MSVC's traditional preprocessor doesn't implement __VA_OPT__, which src/common/reflection.h's MBREFL_STRUCT
            -- leans on heavily -- without it, a macro paste yields a bogus identifier and "undefined type mrbind::Entity" cascades everywhere. /EHsc alongside it: cl.exe/clang-cl default exceptions off, and mrbind throws/catches throughout.
            table.insert(configs, "-DCMAKE_CXX_FLAGS=/Zc:preprocessor /EHsc")
        end

        -- Lets --expose-as-struct accept standard-layout classes with base classes -- what SimpleMath's Vector2/3/4, Quaternion and Color are,
        -- fields inherited from XMFLOAT2/3/4. Size/alignment/offset validation is untouched. Rationale: ../gen_cpp/patches/expose-as-struct-standard-layout-bases.md. KEEP IN SYNC with thirdparty/packages/mrbind.lua.
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
                "mrbind_generators: could not remove the --expose-as-struct no-bases check from " .. f
                .. " -- upstream source moved; see the SDK's feather_cpp/gen_cpp/patches/")
        end

        -- Copies Feather's C++ wrapper generator into the fetched source tree and hooks it into mrbind's own CMakeLists. Grafted rather than
        -- built standalone: mrbind sets -std=c++23/_ITERATOR_DEBUG_LEVEL=0/CMAKE_MSVC_RUNTIME_LIBRARY at directory scope; missing any fails to link (LNK2038). Returns false when the C++ SDK half isn't vendored, so a C/C# plugin never builds a generator it doesn't need.
        local function _graft_feather_gen_cpp()
            local src = FEATHER_GEN_CPP_DIR
            if not os.isdir(src) then
                return false
            end
            os.tryrm("feather_gen_cpp")
            os.cp(src, "feather_gen_cpp")

            -- Explicit binary dir: the executable goes to the build root, so a
            -- build folder named after it would be the linker's output path.
            local line = "add_subdirectory(feather_gen_cpp _feather_gen_cpp_build)"
            local contents = io.readfile("CMakeLists.txt")
            if not contents:find(line, 1, true) then
                io.writefile("CMakeLists.txt", contents:rtrim() .. "\n" .. line .. "\n")
            end
            return true
        end

        _allow_exposed_structs_with_bases()
        local have_gen_cpp = _graft_feather_gen_cpp()

        local builddir = path.join(package:builddir(), ".cmake_build")
        cmake.build(package, configs, {builddir = builddir, cmake_generator = "Ninja"})

        -- No install() rules upstream; copy the generators out by hand. Tries the os.host()-matched .exe spelling first but accepts
        -- either: a Windows PE binary carries .exe regardless of which compiler built it, and this package's own "is this Windows" can't always be trusted either.
        local bindir = package:installdir("bin")
        local tools = {"mrbind_gen_c", "mrbind_gen_csharp"}
        if have_gen_cpp then
            table.insert(tools, "feather_gen_cpp")
        end
        for _, name in ipairs(tools) do
            local preferred = os.host() == "windows" and (name .. ".exe") or name
            local fallback = os.host() == "windows" and name or (name .. ".exe")
            local built = path.join(builddir, preferred)
            if not os.isfile(built) then
                built = path.join(builddir, fallback)
            end
            assert(os.isfile(built), "mrbind_generators: expected build output missing: "
                .. path.join(builddir, preferred) .. " (also checked " .. fallback .. ")")
            os.cp(built, bindir)
        end
    end)

    on_test(function (package)
        os.vrun(path.join(package:installdir("bin"),
            "mrbind_gen_c" .. (os.host() == "windows" and ".exe" or "")) .. " --help")
    end)
package_end()
