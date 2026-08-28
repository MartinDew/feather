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

    on_install(function (package)
        import("package.tools.cmake")

        local configs = {
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "RelWithDebInfo"),
            -- The whole point of this package: no parser, so no
            -- find_package(Clang), so no LLVM anywhere in the build.
            "-DMRBIND_BUILD_PARSER=OFF",
            "-DMRBIND_BUILD_GENERATOR_C=ON",
            "-DMRBIND_BUILD_GENERATOR_CSHARP=ON",
        }

        if package:is_plat("windows") then
            local buildtype_map = {debug = "DEBUG", release = "RELEASE", releasedbg = "RELWITHDEBINFO"}
            local buildtype = buildtype_map[package:mode()] or "RELEASE"
            for _, key in ipairs({
                "CMAKE_C_FLAGS", "CMAKE_C_FLAGS_" .. buildtype,
                "CMAKE_CXX_FLAGS", "CMAKE_CXX_FLAGS_" .. buildtype,
                "CMAKE_EXE_LINKER_FLAGS_" .. buildtype,
            }) do
                table.insert(configs, "-D" .. key .. "=")
            end
        end

        local builddir = path.join(package:builddir(), ".cmake_build")
        cmake.build(package, configs, {builddir = builddir, cmake_generator = "Ninja"})

        -- No install() rules upstream; copy the generators out by hand.
        local bindir = package:installdir("bin")
        for _, name in ipairs({"mrbind_gen_c", "mrbind_gen_csharp"}) do
            local built = path.join(builddir, package:is_plat("windows") and (name .. ".exe") or name)
            assert(os.isfile(built), "mrbind_generators: expected build output missing: " .. built)
            os.cp(built, bindir)
        end
    end)

    on_test(function (package)
        os.vrun(path.join(package:installdir("bin"),
            "mrbind_gen_c" .. (package:is_plat("windows") and ".exe" or "")) .. " --help")
    end)
package_end()
