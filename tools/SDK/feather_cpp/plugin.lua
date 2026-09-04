-- Declares a C++ extension: compiles the generated wrappers, which resolve to
-- the same flat feather_* symbols a C plugin uses.
--
--   opts.files             sources (string or list), required
--   opts.api_json          the designated API file, required
--   opts.engine_binary     Windows only: the file name of the engine executable
--                          the plugin will be loaded into. Defaults to
--                          "feather.exe"; the import table records it, so a
--                          renamed host needs it set.

-- This file's own directory's parent -- inside a function called from the consumer's xmake.lua, os.scriptdir() would resolve
-- to the CONSUMER's directory instead.
local SDK_DIR = path.directory(os.scriptdir())
local SIMPLEMATH_DIR = path.join(SDK_DIR, "feather_cpp", "thirdparty", "SimpleMath")

function feather_cpp_plugin(name, opts)
    opts = opts or {}

    -- The description scope has no assert(); report and skip rather than
    -- failing with an opaque error from add_files().
    if not opts.files then
        print("FeatherPluginSDK: feather_cpp_plugin(\"" .. name .. "\") needs opts.files; skipping.")
        return
    end

    target(name)
        set_kind("shared")
        set_basename(name)
        set_languages("cxx23")
        -- mingw would name this libmy_plugin.dll and MSVC my_plugin.dll -- the .fext manifest has to name one file, so pin the spelling
        -- that does not depend on which toolchain built it.
        if is_plat("windows", "mingw") then
            set_prefixname("")
        end
        -- Flat, not bin/$(mode): the engine finds extensions by walking the project directory, and a per-mode subdirectory would leave
        -- stale copies of other configurations for it to load too.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files(opts.files)
        -- The same SimpleMath the engine compiled, built here rather than linking the engine's -- what makes the math types cross as
        -- themselves: the layouts agree because the sources do, and the generated headers assert it.
        add_files(path.join(SIMPLEMATH_DIR, "SimpleMath.cpp"))
        add_includedirs(SIMPLEMATH_DIR)
        add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")
        -- Generated before the compiler runs; see on_config below.
        add_includedirs(
            path.join(os.projectdir(), "build", "feather_bindings", "include"),
            path.join(os.projectdir(), "build", "feather_bindings", "cpp"))
        add_packages("mrbind_generators", "directxmath")

        -- Only the entry point is meant to be findable; everything else, including this plugin's own copy of SimpleMath's statics,
        -- stays private to the library.
        if not is_plat("windows") then
            add_cxflags("-fvisibility=hidden")
        end

        feather_plugin_link_setup()

        on_config(function (target)
            import("feather_plugin_bindings")
            local out = feather_plugin_bindings.generate(target, opts, {cpp = true})
            feather_plugin_bindings.apply_windows_link(target, opts, out)
        end)
    target_end()
end
