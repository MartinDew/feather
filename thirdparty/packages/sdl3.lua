-- SDL3 custom package (VIDEO subsystem only). Remove if xrepo gains a per-subsystem sdl3 package.
package("sdl3_feather")
    set_kind("library")
    set_homepage("https://libsdl.org")
    set_urls("https://github.com/libsdl-org/SDL.git", {tag = "release-3.4.0"})
    add_deps("cmake")

    on_install(function(package)
        local static = not package:config("shared")
        import("package.tools.cmake").install(package, {
            ["SDL_SHARED"]      = static and "OFF" or "ON",
            ["SDL_STATIC"]      = static and "ON"  or "OFF",
            ["SDL_STATIC_PIC"]  = "ON",
            ["SDL_MAIN"]        = "OFF",
            ["SDL_AUDIO"]       = "OFF",
            ["SDL_VIDEO"]       = "ON",
            ["SDL_GPU"]         = "OFF",
            ["SDL_RENDER"]      = "OFF",
            ["SDL_CAMERA"]      = "OFF",
            ["SDL_JOYSTICK"]    = "OFF",
            ["SDL_HAPTIC"]      = "OFF",
            ["SDL_HIDAPI"]      = "OFF",
            ["SDL_POWER"]       = "OFF",
            ["SDL_SENSOR"]      = "OFF",
            ["SDL_DIALOG"]      = "OFF",
            ["SDL_TESTS"]       = "OFF",
        })
    end)

    -- cmake.install()'s auto-detection only fills sysincludedirs, not linkdirs/links
    on_fetch(function(package)
        local libdir = package:installdir("lib")
        local inc    = package:installdir("include")

        local lib_check = package:is_plat("windows")
            and path.join(libdir, "SDL3-static.lib")
            or  path.join(libdir, "libSDL3.a")
        if not os.isfile(lib_check) then
            return nil
        end

        local info = {
            includedirs = {inc},
            linkdirs    = {libdir},
        }
        if package:config("shared") then
            info.links = {"SDL3"}
        elseif package:is_plat("windows") then
            info.links    = {"SDL3-static"}
            info.syslinks = {"user32", "gdi32", "winmm", "imm32", "ole32", "oleaut32", "version", "uuid", "advapi32", "setupapi", "shell32"}
        else
            info.links = {"SDL3"}
        end
        return info
    end)
package_end()
