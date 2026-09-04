-- Declares a C extension: compiles against the generated C headers and resolves
-- the flat feather_* symbols against the engine process that loads it.
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

function feather_c_plugin(name, opts)
    opts = opts or {}

    -- The description scope has no assert(); report and skip rather than
    -- failing with an opaque error from add_files().
    if not opts.files then
        print("FeatherPluginSDK: feather_c_plugin(\"" .. name .. "\") needs opts.files; skipping.")
        return
    end

    target(name)
        set_kind("shared")
        set_basename(name)
        -- mingw would name this libmy_plugin.dll and MSVC my_plugin.dll -- the .fext manifest has to name one file, so pin the spelling
        -- that does not depend on which toolchain built it.
        if is_plat("windows", "mingw") then
            set_prefixname("")
        end
        -- Flat, not bin/$(mode): the engine finds extensions by walking the project directory, and a per-mode subdirectory would leave
        -- stale copies of other configurations for it to load too.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files(opts.files)
        -- Generated before the compiler runs; see on_config below.
        add_includedirs(path.join(os.projectdir(), "build", "feather_bindings", "include"))
        add_packages("mrbind_generators")

        feather_plugin_link_setup()

        -- on_config, not before_build: the include directory above needs real headers before the compiler runs, and on_config runs
        -- serially in dependency order. opts is captured as an upvalue -- only *globals* differ between scopes, and set_values() can't carry a table.
        on_config(function (target)
            import("feather_plugin_bindings")
            local out = feather_plugin_bindings.generate(target, opts, {})
            feather_plugin_bindings.apply_windows_link(target, opts, out)
        end)
    target_end()
end
