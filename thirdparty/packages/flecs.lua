-- flecs, built with DEFAULT symbol visibility instead of hidden, so
-- -rdynamic can re-export ecs_* to project DLLs. Otherwise a copy of
-- xmake-repo's packages/f/flecs/xmake.lua.
package("flecs")
    set_homepage("https://github.com/SanderMertens/flecs")
    set_description("A fast entity component system (ECS) for C & C++, built with default symbol visibility")
    set_license("MIT")

    add_urls("https://github.com/SanderMertens/flecs/archive/refs/tags/$(version).tar.gz",
             "https://github.com/SanderMertens/flecs.git")

    -- Only the version the engine pins, forcing a check the patch below still applies on a bump.
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
        -- Fail loudly: a no-op patch here silently reintroduces the project-DLL startup crash.
        assert(io.readfile(visibility_cmake) ~= before,
            "flecs no longer sets 'C_VISIBILITY_PRESET hidden' in " .. visibility_cmake ..
            "; re-check how ecs_* symbols get exported before removing this patch")

        local configs = {}
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))
        table.insert(configs, "-DFLECS_STATIC=" .. (package:config("shared") and "OFF" or "ON"))
        table.insert(configs, "-DFLECS_SHARED=" .. (package:config("shared") and "ON" or "OFF"))
        table.insert(configs, "-DFLECS_PIC=" .. (package:config("pic") and "ON" or "OFF"))
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
