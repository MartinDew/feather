set_xmakever("2.9.0")
set_project("feather")
set_version("1.0.0")
set_languages("cxx23", "clatest")

-- Custom import()-able modules (xmake/modules/*.lua), e.g. feather_codegen --
-- registered before anything that might import() from a script-scope closure.
add_moduledirs(path.join(os.scriptdir(), "xmake", "modules"))

-- ---- Options and helpers ------------------------------------------------
includes("xmake/options.lua")
includes("xmake/helper.lua")

-- ---- Build modes --------------------------------------------------------
-- debug      -> CMake Debug       (-O0, symbols, BETA, no NDEBUG)
-- releasedbg -> CMake Development (-O2, symbols, BETA, no NDEBUG)
-- release    -> CMake Release     (-O3, NDEBUG)
add_rules("mode.debug", "mode.releasedbg", "mode.release")

-- mode.release hides symbols by default, which defeats -rdynamic and breaks
-- dlopen'd project DLLs. Set globally so linked-in static libs match too.
if is_mode("release") then
    set_symbols("none")
end

-- Keep compile_commands.json up to date for clangd / clang-tidy
add_rules("plugin.compile_commands.autoupdate", {outputdir = "$(builddir)"})

-- ---- Global platform defines --------------------------------------------
if is_plat("windows") then
    add_defines("NOMINMAX", "WIN32_LEAN_AND_MEAN")
end
if is_plat("macosx") then
    add_cxflags("-fexperimental-library", {force = true})
end

-- ---- LTO ----------------------------------------------------------------
if has_config("production") or has_config("use_lto") then
    set_policy("build.optimization.lto", true)
end

-- ---- Static C++ runtime on MSVC (set_runtimes must be global) -----------
if has_config("production") or has_config("static_cpp") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
end

-- ---- Third-party packages and local targets -----------------------------
includes("thirdparty/xmake.lua")

-- ---- feather_public_api -------------------------------------------------
-- Headeronly umbrella target: engine include dirs + PUBLIC thirdparty headers, without a module -> executable circular dep.
includes("xmake/public_api.lua")

-- ---- Bindings API ---------------------------------------------------------
-- The shared MRBind parse (build/bindings/api.json) that modules/*_bindings generate from -- before the engine targets, whose
-- dependencies on it must already be defined. Every language is generated from the C bindings, so only that option gates the parse.
if has_config("enable_c_bindings") then
    includes("xmake/bindings.lua")
end

-- ---- Engine targets -------------------------------------------------------
-- The feather executable, its core sources, codegen wiring, and modules. Split out so it can be includes()'d cross-repo without
-- dragging along set_project()/set_version()/etc, which only make sense at this top level.
includes("xmake/engine.lua")

-- ---- API export -----------------------------------------------------------
-- Publishes the API description a plugin project designates in its own build -- the handoff point of the whole multi-language plugin
-- story: the engine parses its headers once (xmake/bindings.lua), and this task rewrites the two machine-specific path prefixes out of
-- it so the result describes the API and nothing about this checkout. See tools/SDK/FeatherPluginSDK.lua.
task("export-api")
    set_menu {
        usage = "xmake export-api",
        description = "Publish build/bindings/dist/feather_api.json for plugin projects to consume",
    }

    on_run(function ()
        import("feather_bindings")

        local api_json = feather_bindings.api_json_path()
        assert(os.isfile(api_json), "export-api: " .. api_json .. " does not exist.\n"
            .. "  Configure with the C bindings enabled first: xmake f -m debug -y")

        -- Forward slashes: this is how the parse spelled them inside api.json,
        -- on whatever platform produced it.
        local feather_root = feather_bindings.to_forward_slashes(os.scriptdir())

        -- Every filename in the parse is absolute, and a consumer's generator must be told which prefix to strip. Replaced by tokens the
        -- SDK substitutes back to directories of its own (feather_plugin_bindings.resolve_api_json), so the published file names no machine.
        local content = io.readfile(api_json)

        -- Read out of the parse rather than asked of the current configuration: a package's install directory carries a hash of the config
        -- that built it, so the two disagree whenever api.json predates a reconfigure.
        local directxmath_root = feather_bindings.directxmath_root_in(content)
        -- DirectXMath first: its package directory lives under the engine root, so replacing the root first would bury the longer prefix.
        if directxmath_root then
            content = content:replace(directxmath_root, feather_bindings.directxmath_token(), {plain = true})
        end
        content = content:replace(feather_root, feather_bindings.feather_token(), {plain = true})

        assert(content:find(feather_bindings.feather_token(), 1, true),
            "export-api: no filename in " .. api_json .. " starts with " .. feather_root
            .. " -- the parse was produced from a different checkout")
        -- Any absolute path left is one the substitutions above don't know about, and would send a consumer's generator looking for a
        -- directory only this machine has.
        local leftover = content:match('"(/[^/*][^"]*)"') or content:match('"(%a:/[^"]*)"')
        assert(not leftover, "export-api: an absolute path survived the rewrite: "
            .. tostring(leftover) .. "\n  It needs a token of its own, like the two above.")

        os.mkdir(feather_bindings.dist_dir())
        io.writefile(feather_bindings.dist_api_json_path(), content)
        cprint("${green}export-api:${reset} %s", feather_bindings.dist_api_json_path())

        -- One file, on every platform, nothing binary -- in particular no import library: a Windows plugin needs one, but the SDK
        -- builds it locally from the descriptor this file produces (apply_windows_link), rather than shipping a per-build binary.
    end)
task_end()
