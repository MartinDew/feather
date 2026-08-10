-- flecs, built with DEFAULT symbol visibility instead of hidden.
--
-- Why this shadow package exists
-- ------------------------------
-- A project DLL (see tools/SDK/FeatherSDK.lua) must not link its own copy of
-- flecs: flecs keeps process-global mutable state -- ecs_os_api, the builtin
-- component-id globals -- that only the engine EXECUTABLE initializes. A DLL
-- with a private static copy gets a second, all-zero ecs_os_api and segfaults
-- on a NULL function pointer the first time it imports an ECS module.
-- xmake/public_api.lua therefore hands consumers flecs headers with no links,
-- so every ecs_* stays undefined in the DLL and the loader binds it to the
-- already-running host executable, which exports its symbols via
-- add_ldflags("-rdynamic") in the root xmake.lua.
--
-- That only works if the host actually exports them, and stock flecs does not.
-- flecs's own cmake/target_default_compile_options.cmake unconditionally does
--
--     set_target_properties(${THIS} PROPERTIES ... C_VISIBILITY_PRESET hidden)
--
-- and a TARGET property always beats the global CMAKE_C_VISIBILITY_PRESET
-- variable, so passing -DCMAKE_C_VISIBILITY_PRESET=default on the command line
-- has no effect. Nor does -fvisibility=default in CMAKE_C_FLAGS: cmake appends
-- the property's -fvisibility=hidden afterwards, and the last one wins.
--
-- Normally the per-declaration FLECS_API macro would override the preset, but
-- for the static library it expands to nothing: CMakeLists.txt does
-- `target_compile_definitions(flecs_static PUBLIC flecs_STATIC)`, which sends
-- include/flecs/bake_config.h down its `#else #define FLECS_API` branch. So on
-- a stock static build 637 of the 665 ecs_* definitions come out GLOBAL HIDDEN
-- and -rdynamic cannot re-export them. Patching the preset is the only knob.
--
-- (Building flecs SHARED would avoid the patch -- cmake auto-defines
-- flecs_EXPORTS for the shared target, which gives every public declaration an
-- explicit visibility("default") -- but that means shipping a libflecs.so next
-- to every binary. Deliberately not the route taken here.)
--
-- Everything below other than the patch and the extra -D flags is a copy of
-- xmake-repo's packages/f/flecs/xmake.lua; keep it in sync when bumping.
package("flecs_feather")
    set_homepage("https://github.com/SanderMertens/flecs")
    set_description("A fast entity component system (ECS) for C & C++, built with default symbol visibility")
    set_license("MIT")

    add_urls("https://github.com/SanderMertens/flecs/archive/refs/tags/$(version).tar.gz",
             "https://github.com/SanderMertens/flecs.git")

    -- Only the version the engine pins. A bump has to come here first, which is
    -- the point: it forces a look at whether the patch below still applies.
    add_versions("v4.1.5", "8b94f56dfdda0b3c86110f651a4e0ec1c59030db43bb4810ae296a0630682ab9")

    add_deps("cmake")

    if is_plat("windows", "mingw") then
        add_syslinks("wsock32", "ws2_32", "Dbghelp")
    elseif is_plat("linux") then
        add_syslinks("pthread")
    elseif is_plat("bsd") then
        add_syslinks("execinfo", "pthread")
    end

    on_load("windows", "mingw", function (package)
        if not package:config("shared") then
            package:add("defines", "flecs_STATIC")
        end
    end)

    on_install(function (package)
        local visibility_cmake = path.join("cmake", "target_default_compile_options.cmake")
        local before = io.readfile(visibility_cmake)
        io.replace(visibility_cmake,
            "C_VISIBILITY_PRESET hidden",
            "C_VISIBILITY_PRESET default",
            {plain = true})
        -- Fail loudly rather than silently producing a hidden-symbol archive:
        -- a no-op patch reintroduces the project-DLL startup crash, and that
        -- only shows up at runtime, in a dlopen'd library, as a NULL call.
        assert(io.readfile(visibility_cmake) ~= before,
            "flecs no longer sets 'C_VISIBILITY_PRESET hidden' in " .. visibility_cmake ..
            "; re-check how ecs_* symbols get exported before removing this patch " ..
            "(see the comment at the top of thirdparty/packages/flecs.lua)")

        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DFLECS_STATIC=" .. (package:config("shared") and "OFF" or "ON"))
        table.insert(configs, "-DFLECS_SHARED=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DFLECS_PIC=" .. (package:config("pic") and "ON" or "OFF"))
        -- Belt and braces: these don't override the target property on their
        -- own (that's what the patch is for), but they keep anything flecs adds
        -- later without a target property from defaulting to hidden.
        table.insert(configs, "-DCMAKE_C_VISIBILITY_PRESET=default")
        table.insert(configs, "-DCMAKE_CXX_VISIBILITY_PRESET=default")
        table.insert(configs, "-DCMAKE_VISIBILITY_INLINES_HIDDEN=OFF")
        import("package.tools.cmake").install(package, configs)

        local pdb = path.join(package:buildir(), "flecs.pdb")
        os.trycp(pdb, package:installdir(package:config("shared") and "bin" or "lib"))
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() {
                flecs::world ecs;
            }
        ]]}, {configs = {languages = "c++17"}, includes = "flecs.h"}))
    end)
package_end()
