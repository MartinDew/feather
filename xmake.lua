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
-- dependencies on it must already be defined.
if has_config("enable_c_bindings", "enable_cs_bindings", "enable_py_bindings") then
    includes("xmake/bindings.lua")
end

-- ---- Engine targets -------------------------------------------------------
-- The feather executable, its core sources, codegen wiring, and modules. Split out so it can be includes()'d cross-repo without
-- dragging along set_project()/set_version()/etc, which only make sense at this top level.
includes("xmake/engine.lua")

-- ---- API export -----------------------------------------------------------
-- Publishes the API description a plugin project designates in its own build -- the handoff point of the whole multi-language plugin
-- story: the engine parses its headers once (xmake/bindings.lua), and this task copies out everything a plugin needs plus a sidecar describing how it was made. See tools/SDK/FeatherPluginSDK.lua.
task("export-api")
    set_menu {
        usage = "xmake export-api",
        description = "Publish build/bindings/dist/feather_api.json for plugin projects to consume",
    }

    on_run(function ()
        import("core.base.json")
        import("core.project.project")
        import("feather_bindings")

        local api_json = feather_bindings.api_json_path()
        assert(os.isfile(api_json), "export-api: " .. api_json .. " does not exist.\n"
            .. "  Configure with the C bindings enabled first: xmake f -m debug -y")

        -- Normalized to forward slashes: handed back verbatim to the generators by every consumer, on whatever platform, and must
        -- match the filenames the parse recorded inside api.json.
        local feather_root = feather_bindings.to_forward_slashes(os.scriptdir())
        local dist = feather_bindings.dist_dir()
        os.mkdir(dist)
        os.vcp(api_json, feather_bindings.dist_api_json_path())

        local commit = try { function ()
            return os.iorunv("git", {"describe", "--always", "--dirty"}, {curdir = feather_root}):trim()
        end } or "unknown"

        json.savefile(feather_bindings.dist_api_meta_path(), {
            api_version = 1,
            -- The engine checkout path baked into every filename inside api.json. A consumer passes it back to the generators verbatim
            -- so their path mappings line up; nothing ever opens it, so it needs not exist on the consumer's machine.
            feather_root = feather_root,
            -- Likewise for DirectXMath's headers, reached through SimpleMath's bases and living outside the engine tree, so they
            -- need a path mapping of their own.
            directxmath_root = feather_bindings.to_forward_slashes(
                feather_bindings.directxmath_includedir(project.target("feather"))),
            engine_commit = commit,
            gen_c_flags_id = feather_bindings.gen_c_flags_id(),
            -- Which mrbind produced this file. The schema is undocumented, so
            -- a consumer whose generators disagree can at least be told why.
            mrbind_ref = feather_bindings.mrbind_pinned_commit(),
            generated = os.date("%Y-%m-%dT%H:%M:%S"),
        })

        cprint("${green}export-api:${reset} %s", feather_bindings.dist_api_json_path())
        cprint("${green}export-api:${reset} %s", feather_bindings.dist_api_meta_path())

        -- Two files, on every platform, nothing binary -- in particular no import library: a Windows plugin needs one, but the SDK
        -- builds it locally from the descriptor these files produce (apply_windows_link), rather than shipping a per-build binary.
    end)
task_end()
