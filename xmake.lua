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
-- Headeronly umbrella target: engine include dirs + PUBLIC thirdparty
-- headers, without a module -> executable circular dep. Also the source of
-- truth for downstream consumers via tools/SDK/FeatherSDK.lua.
includes("xmake/public_api.lua")

-- ---- Bindings API ---------------------------------------------------------
-- The shared MRBind parse (build/bindings/api.json) that modules/*_bindings
-- generate from. Before the engine targets: they depend on it, and a target's
-- dependencies must already be defined.
if has_config("enable_c_bindings", "enable_cs_bindings", "enable_py_bindings") then
    includes("xmake/bindings.lua")
end

-- ---- Engine targets -------------------------------------------------------
-- The feather executable, its core sources, codegen wiring, and modules.
-- Split out so it can be includes()'d cross-repo without dragging along
-- set_project()/set_version()/etc, which only make sense at this top level.
includes("xmake/engine.lua")
