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
-- Feather's own C++ wrapper generator (../gen_cpp) is grafted into this source
-- tree and installed as a third tool, feather_gen_cpp. See
-- _graft_feather_gen_cpp below for why it is built here rather than separately.

-- Where the grafted generator's sources live. Captured as a value while this
-- file is being loaded, not computed inside a callback: os.scriptdir() resolves
-- against whichever script the sandbox is currently running, which during
-- on_install is not this file. Same capture-at-include pattern as
-- FeatherPluginSDK.lua's SDK_DIR.
--
-- One level up from <sdk>/packages is the SDK root -- not os.projectdir(),
-- which is the plugin project's own directory when this is vendored into one.
local FEATHER_GEN_CPP_DIR = path.join(path.directory(os.scriptdir()), "gen_cpp")

-- Content hash of the grafted generator's sources, for the requiring side to
-- pass as a package config. A package's install hash comes from the configs it
-- was asked for, so one the package set itself would never invalidate it and an
-- edit here would silently keep the old binary.
-- Global, so an includes()'ing description file can call it.
-- KEEP IN SYNC with thirdparty/packages/mrbind.lua.
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

        -- Which compiler actually builds this on Windows is not something we
        -- get to assume -- it depends on what a left-to-its-own-devices CMake
        -- configure finds first, which varies by machine, and even a
        -- deliberately forced choice can turn out wrong in a way no flag
        -- fixes: real cl.exe rejects mrbind's own use of C++23's auto(x)
        -- decay-copy (src/common/strings.h) with a hard parser error on
        -- every MSVC toolset through the whole VS 2022 generation -- it's
        -- simply missing until MSVC 19.50 / VS 2026, not a matter of flags.
        --
        -- clang-cl sidesteps that: same MSVC-style flag syntax as cl.exe (so
        -- the fixes below still apply to it), but Clang's frontend has
        -- supported auto(x) for a long time, independent of whichever VS
        -- ships on a given machine. Preferred over cl.exe for exactly that
        -- reason -- it is also the toolchain this project's own Windows CI
        -- leg is configured for (--toolchain=clang-cl), which real cl.exe was
        -- never actually part of the intent for, just what a bare
        -- find_tool("cl") happened to prefer once it existed.
        --
        -- When neither is found, nothing here is added and CMake configures
        -- however it already does successfully: plain clang++, which needs
        -- none of this -- confirmed by a run where this whole block was
        -- accidentally skipped and clang++ still built and linked both
        -- generators cleanly on its own. It would, however, reject the
        -- MSVC-syntax flag this block passes outright ("clang++: error: no
        -- such file or directory: '/Zc:preprocessor'", a bare '/whatever'
        -- reading as a path to a GNU-style driver), which is why the choice
        -- has to be pinned explicitly rather than left for CMake to mix.
        local compiler = os.host() == "windows" and (find_tool("clang-cl") or find_tool("cl")) or nil
        if compiler then
            table.insert(configs, "-DCMAKE_C_COMPILER=" .. compiler.program)
            table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. compiler.program)

            -- mrbind's own CMakeLists.txt only requests a C++ standard on
            -- `if(NOT MSVC)` -- and CMake's MSVC variable is true for
            -- clang-cl too (its whole point is presenting an MSVC-compatible
            -- frontend), so this is skipped for both. Left unset, either one
            -- silently compiles in a pre-C++17 mode and <filesystem> in
            -- src/common/filesystem.h fails to find std::filesystem at all.
            --
            -- CMAKE_CXX_STANDARD, not a raw /std: flag: CMakeLists.txt never
            -- sets it itself (the assignment is commented out, "old CMake
            -- doesn't understand it"), so a target's CXX_STANDARD property
            -- defaults to whatever the command line supplied -- this is the
            -- one case where setting the CMake variable from outside actually
            -- takes effect, rather than being shadowed by the project's own
            -- assignment the way CMAKE_CXX_FLAGS is below.
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

            -- MSVC's traditional preprocessor does not implement __VA_OPT__,
            -- and /std:c++latest does not switch preprocessors -- for C++ that
            -- needs /Zc:preprocessor explicitly (unlike C, where /std:c11 and
            -- later imply it).
            --
            -- src/common/reflection.h leans on __VA_OPT__ heavily: MBREFL_STRUCT
            -- pastes DETAIL_MBREFL_STRUCT_INIT_ onto the result of a macro whose
            -- whole body is `__VA_OPT__(1)`. Without the conforming preprocessor
            -- that token survives literally and the paste yields the identifier
            -- DETAIL_MBREFL_STRUCT_INIT___VA_OPT__, which is the error actually
            -- reported; every "undefined type mrbind::Entity" after it is
            -- fallout from the struct never being declared.
            --
            -- Set as CMAKE_CXX_FLAGS rather than added to the cleared list
            -- above, since the project appends its own -D_ITERATOR_DEBUG_LEVEL=0
            -- to this same variable and both have to survive.
            --
            -- /EHsc alongside it: cl.exe and clang-cl both default to
            -- exceptions off unless told otherwise (unlike plain clang++,
            -- which defaults them on -- part of why that one needed nothing
            -- here). mrbind's source throws/catches throughout, and without
            -- this every one of those sites fails with "cannot use 'throw'
            -- with exceptions disabled" -- the whole build, not one file.
            table.insert(configs, "-DCMAKE_CXX_FLAGS=/Zc:preprocessor /EHsc")
        end

        -- Lets --expose-as-struct accept standard-layout classes that have base
        -- classes -- what SimpleMath's Vector2/3/4, Quaternion and Color are,
        -- with their fields inherited from XMFLOAT2/3/4. The size/alignment/
        -- offset validation mrbind does when emitting the struct is untouched,
        -- so an unsuitable type still fails loudly. Rationale in full:
        -- ../gen_cpp/patches/expose-as-struct-standard-layout-bases.md.
        -- KEEP IN SYNC with thirdparty/packages/mrbind.lua.
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
                .. " -- upstream source moved; see the SDK's gen_cpp/patches/")
        end

        -- Copies Feather's C++ wrapper generator into the fetched source tree
        -- and hooks it into mrbind's own CMakeLists. Grafted rather than built
        -- standalone because mrbind sets -std=c++23, _ITERATOR_DEBUG_LEVEL=0 and
        -- CMAKE_MSVC_RUNTIME_LIBRARY at directory scope, and a target missing any
        -- of them fails to link against mrbind_c_interop (LNK2038 on Windows).
        -- KEEP IN SYNC with thirdparty/packages/mrbind.lua.
        -- Returns false when the C++ half of the SDK is not vendored: a C or C#
        -- plugin needs no wrapper generator, and must not be made to build one.
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

        -- No install() rules upstream; copy the generators out by hand.
        --
        -- Tried without an .exe suffix first when os.host() isn't "windows"
        -- and with one otherwise, but a mismatch between the two isn't fatal
        -- on its own: whichever spelling the build actually produced is
        -- accepted, since a Windows PE binary carries .exe regardless of
        -- which compiler built it, and the point of the os.host() switch
        -- above is precisely that this package's own view of "is this
        -- Windows" cannot always be trusted for that decision either.
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
