-- FeatherPluginSDK: builds Feather extensions written in C or C# against a
-- designated API description file, with no engine checkout involved.
--
-- This is the counterpart to tools/SDK/FeatherSDK.lua, which builds C++
-- extensions and does need the engine's headers, its compile flags and its
-- reflection codegen. Nothing here needs any of that:
--
--   * The engine parsed its own headers once and published the result as
--     feather_api.json (see the `export-api` task). A plugin turns that JSON
--     into C headers or C# sources with mrbind's generators, which link no
--     Clang and need no engine source -- so this file, the modules/ and
--     packages/ directories next to it, and an api/ file are the whole
--     toolchain.
--   * mrbind also emits C++ glue that calls the engine. It is deliberately
--     never compiled here: the engine already has that glue compiled into its
--     own binary. A plugin's feather_* imports stay undefined and bind to the
--     engine process when it dlopens the plugin.
--
-- Vendor this file, modules/, packages/ and the api/ files into the plugin
-- repo -- the same way a Godot project vendors nothing but its .gdextension.
-- See tools/templates/plugin_c_template/.
--
-- Usage (in the plugin's xmake.lua):
--
--     includes("sdk/FeatherPluginSDK.lua")
--     feather_plugin_sdk_init()
--
--     feather_c_plugin("my_plugin", {
--         files    = "src/*.c",
--         api_json = "api/feather_api.json",
--     })
--
-- The real work lives in modules/feather_plugin_bindings.lua. It has to: a
-- function defined in an includes()'d file like this one keeps the description
-- sandbox as its environment even when called from on_config(), and that
-- sandbox has no assert(), import() or io.

-- This file's own directory, captured while it is being included. Inside a
-- function called from the consumer's xmake.lua, os.scriptdir() would be the
-- CONSUMER's directory instead.
local SDK_DIR = os.scriptdir()

-- Call once, before any feather_*_plugin().
function feather_plugin_sdk_init()
    add_moduledirs(path.join(SDK_DIR, "modules"))
    includes(path.join(SDK_DIR, "packages", "mrbind_generators.lua"))
    -- host = true: these are build tools this machine runs, not libraries the
    -- plugin links, so a cross-compiling plugin build still gets runnable ones.
    add_requires("mrbind_generators", {system = false, host = true})
end

-- Shared link setup: a plugin links nothing of the engine's.
--
-- On ELF its feather_* imports stay undefined and bind when the engine dlopens
-- it, against the engine executable itself -- which exports the generated C
-- bindings along with the rest of its API (it links -rdynamic, and the bindings
-- are compiled into it). That is the same arrangement C++ extensions use for
-- engine symbols (tools/SDK/FeatherSDK.lua), and it is what keeps a built
-- plugin independent of where the engine lives.
-- Windows linking is handled in on_config instead (see the module's
-- apply_windows_link): PE has no load-time binding, so the plugin needs an
-- import library -- which is generated there from the API descriptor, not
-- shipped, and the description scope can neither run a tool nor fail cleanly.
local function apply_plugin_link_setup()
    if is_plat("macosx") then
        -- Mach-O rejects undefined symbols in a dylib by default.
        add_shflags("-undefined", "dynamic_lookup", {force = true})
    end
end

-- Declares a C extension.
--
--   opts.files             sources (string or list), required
--   opts.api_json          the designated API file, required
--   opts.api_meta          defaults to <api_json basename>.meta.json alongside it
--   opts.engine_binary     Windows only: the file name of the engine executable
--                          the plugin will be loaded into. Defaults to
--                          "feather.exe"; the import table records it, so a
--                          renamed host needs it set.
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
        -- mingw would name this libmy_plugin.dll and MSVC my_plugin.dll. The
        -- .fext manifest has to name one file, so pin the spelling that does
        -- not depend on which toolchain built it.
        if is_plat("windows", "mingw") then
            set_prefixname("")
        end
        -- Flat, not bin/$(mode): the engine finds extensions by walking the
        -- project directory, and a per-mode subdirectory would leave stale
        -- copies of other configurations for it to load too.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files(opts.files)
        -- Generated before the compiler runs; see on_config below.
        add_includedirs(path.join(os.projectdir(), "build", "feather_bindings", "include"))
        add_packages("mrbind_generators")

        apply_plugin_link_setup()

        -- on_config, not before_build: the include directory above has to hold
        -- real headers before the compiler is invoked, and on_config is the
        -- phase that runs serially, in dependency order.
        -- opts is captured as an upvalue. Only the sandbox's *globals* differ
        -- between description and script scope; upvalues cross that boundary
        -- fine, and set_values() cannot carry a table.
        on_config(function (target)
            import("feather_plugin_bindings")
            local out = feather_plugin_bindings.generate(target, opts, false)
            feather_plugin_bindings.apply_windows_link(target, opts, out)
        end)
    target_end()
end

-- Declares a C# extension, published with NativeAOT so the result is an
-- ordinary native shared library -- the engine loads it exactly like a C one
-- and hosts no .NET runtime of its own.
--
--   opts.csproj          the project file, required
--   opts.api_json        the designated API file, required
--   opts.published_name  file dotnet publish emits (default: the csproj's own
--                          filename plus the host's native shared-library
--                          extension -- .dll/.so/.dylib; override if
--                          <AssemblyName> in the .csproj differs)
--   opts.output_name     name to stage into bin/ as (default: lib<name>.so,
--                          matching feather_c_plugin's own naming -- <name>.dll
--                          with no "lib" prefix on Windows, lib<name>.dylib on
--                          macOS)
--   opts.runtime         .NET RID passed to dotnet publish (default: the host's
--                          own RID -- NativeAOT cannot cross the OS boundary,
--                          so this is the only default that always works)
function feather_cs_plugin(name, opts)
    opts = opts or {}

    target(name)
        -- Phony: dotnet does the building. xmake only sequences it and stages
        -- the result.
        set_kind("phony")
        add_packages("mrbind_generators")

        on_build(function (target)
            import("feather_plugin_bindings")
            local out = feather_plugin_bindings.generate(target, opts, true)
            feather_plugin_bindings.publish_csharp(target, opts, out)
        end)
    target_end()
end
