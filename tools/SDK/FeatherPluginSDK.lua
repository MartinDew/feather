-- FeatherPluginSDK: builds Feather extensions written in C, C++ or C# against a
-- designated API description file, with no engine checkout involved.
--
-- Every language goes through the same door -- the engine's C bindings -- so
-- none of them needs the engine's headers, its compile flags or its reflection
-- codegen:
--
--   * The engine parsed its own headers once and published the result as
--     feather_api.json (see the `export-api` task). A plugin turns that JSON
--     into C headers, C++ wrappers or C# sources with generators that link no
--     Clang and need no engine source.
--   * mrbind also emits C++ glue that calls the engine. It is deliberately
--     never compiled here: the engine already has that glue compiled into its
--     own binary. A plugin's feather_* imports stay undefined and bind to the
--     engine process when it dlopens the plugin.
--
-- Vendor this file, modules/, packages/ and the feather_<lang>/ directory for
-- each language you build, alongside an api/ file -- the same way a Godot
-- project vendors nothing but its .gdextension. A language whose directory is
-- absent simply has no feather_<lang>_plugin().
-- See tools/templates/plugin_{c,cpp,cs}_template/.
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

-- This file's own directory, captured while it is being included -- inside a function called from the consumer's xmake.lua, os.scriptdir()
-- would resolve to the CONSUMER's directory instead.
local SDK_DIR = os.scriptdir()

-- The C++ half of the SDK (wrapper generator, headers, math sources) is optional.
-- A C or C# plugin vendors none of it and must not be made to build a generator it never runs, nor fetch DirectXMath.
local HAVE_CPP_SDK = os.isdir(path.join(SDK_DIR, "feather_cpp", "gen_cpp"))

-- Shared link setup: a plugin links nothing of the engine's. On ELF its feather_* imports stay undefined and bind against the running engine
-- executable (-rdynamic, bindings compiled in) when it dlopens the plugin. Windows instead gets an import library in on_config (apply_windows_link), since PE has no load-time binding and the description scope can't run a tool.
-- Global, not local: the per-language plugin.lua files included below call it.
function feather_plugin_link_setup()
    if is_plat("macosx") then
        -- Mach-O rejects undefined symbols in a dylib by default.
        add_shflags("-undefined", "dynamic_lookup", {force = true})
    end
end

-- Call once, before any feather_*_plugin().
function feather_plugin_sdk_init()
    add_moduledirs(path.join(SDK_DIR, "modules"))
    includes(path.join(SDK_DIR, "packages", "mrbind_generators.lua"))
    if HAVE_CPP_SDK then
        -- Header-only, and the C++ wrappers alias its types rather than
        -- wrapping them; a C or C# plugin never resolves it.
        includes(path.join(SDK_DIR, "feather_cpp", "packages", "directxmath.lua"))
        add_requires("directxmath_feather", {system = false, alias = "directxmath"})
    end
    -- host = true: these are build tools this machine runs, not libraries the
    -- plugin links, so a cross-compiling plugin build still gets runnable ones.
    add_requires("mrbind_generators", {system = false, host = true,
        configs = {gen_cpp_rev = feather_gen_cpp_rev(path.join(SDK_DIR, "feather_cpp", "gen_cpp"))}})
end

-- Each language's feather_<lang>_plugin() lives beside the files it needs, so a
-- plugin repo vendors only the languages it builds.
local function have_lang(lang)
    local plugin_lua = path.join(SDK_DIR, "feather_" .. lang, "plugin.lua")
    if os.isfile(plugin_lua) then
        includes(plugin_lua)
        return true
    end
    return false
end

-- A language whose directory wasn't vendored gets a stub saying so, rather than failing as an unknown global.
-- Assigned by name, one per language: the description sandbox has no _G to index.
local function missing(lang)
    print("FeatherPluginSDK: this project calls feather_" .. lang .. "_plugin(), but the SDK's feather_"
        .. lang .. "/ directory was not vendored; skipping.")
end

if not have_lang("c") then
    function feather_c_plugin() missing("c") end
end
if not have_lang("cpp") then
    function feather_cpp_plugin() missing("cpp") end
end
if not have_lang("cs") then
    function feather_cs_plugin() missing("cs") end
end
