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

-- FEATHER_API/FEATHER_INTERNAL (core/framework/feather_api.h) is the declared
-- plugin ABI surface now (see the plugin-abi-rework plan's Stage 3) --
-- -fvisibility=hidden hides everything else by default, and -rdynamic
-- (xmake/engine.lua) re-exports exactly the annotated surface from .dynsym.
--
-- Applied as raw cxflags, not set_symbols("hidden"): that setter holds a
-- single value that mode.debug/mode.releasedbg already use for "debug" (the
-- -g flag), so calling it here would silently cost us debug symbols in every
-- non-release mode. cxflags is a separate axis from symbols, so all three
-- modes share one ABI without disturbing -g.
--
-- Global (every binary and static target) via description-scope add_cxflags,
-- same reasoning the old set_symbols("none")-on-release-only block here used
-- to give: the objects linked into feather.<variant> are compiled under
-- their OWN targets (feather_module_target static libs, simplemath), so each
-- needs the same visibility or a hidden symbol in one archive silently
-- reappears default the moment a differently-configured object defining the
-- same symbol links ahead of it. A downstream project DLL is a SEPARATE
-- xmake project, though (its own xmake.lua, never includes() this file), so
-- it doesn't inherit this -- xmake/modules/feather_flags.lua's apply()
-- carries the same flag to FeatherSDK.lua's consumer target too.
--
-- GCC/Clang-only syntax, guarded off real MSVC (cl.exe): MSVC's model is the
-- opposite default (nothing exported unless __declspec(dllexport), which
-- feather_api.h already provides) rather than something -fvisibility tunes,
-- and passing it to cl.exe would just be an unrecognized-flag warning at
-- best. "windows" excludes mingw deliberately -- is_plat() gives mingw its
-- own value, and mingw's GCC-based toolchain wants this flag same as Linux.
if not is_plat("windows") then
    add_cxflags("-fvisibility=hidden", "-fvisibility-inlines-hidden", {force = true})
end

if is_plat("windows") then
    -- MSVC warns C4251/C4275 on every STL-by-value member or base of an
    -- exported class (e.g. ClassDB::_class_infos, ResourceLoader::_cache) --
    -- the exact hazard the engine/plugin shared-CRT requirement (this file's
    -- static_cpp block) already resolves, so silence rather than fight it.
    add_cxflags("/wd4251", "/wd4275", {force = true})
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

-- ---- C++ runtime on MSVC (set_runtimes must be global) -------------------
-- Static in shipping mode; dynamic otherwise (plugin-abi-rework plan,
-- decision 7). The dynamic branch is not cosmetic: a project DLL is a
-- SEPARATE xmake project (tools/SDK/FeatherSDK.lua, includes()'d into the
-- consumer's own xmake.lua) that resolves its own default runtime
-- independently of this one. Observed directly in CI: with neither branch
-- forced, feather.exe defaulted to MTd (static) while feather-example-
-- project's example.dll defaulted to MDd (dynamic) -- same mode, same
-- --toolchain=clang-cl, different xmake invocations, different silent
-- defaults. A /MT host loading a /MD plugin shares no CRT state (heap,
-- iostream, locale) across the boundary, which is exactly why the plugin
-- smoke test hung inside LoadLibrary on Windows with zero diagnostic
-- output: not a build-order or symbol-visibility bug, a CRT mismatch.
-- Pinning both sides to the same explicit string (this file and
-- FeatherSDK.lua's matching block) removes the ambiguity instead of hoping
-- two independent xmake resolutions agree.
if has_config("production") or has_config("static_cpp") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
elseif is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

-- ---- Third-party packages and local targets -----------------------------
includes("thirdparty/xmake.lua")

-- ---- feather_public_api -------------------------------------------------
-- Headeronly umbrella target giving modules engine include dirs and PUBLIC
-- thirdparty headers without a module -> executable circular dep. Also the
-- single source of truth for downstream "project DLL" consumers, via
-- tools/SDK/FeatherSDK.lua -- see xmake/public_api.lua.
includes("xmake/public_api.lua")

-- ---- Engine targets -------------------------------------------------------
-- The feather executable, its core sources, codegen wiring, and modules.
-- Split into its own file (rather than inline here, as before) so it can be
-- includes()'d cross-repo by a future consumer build without dragging along
-- set_project()/set_version()/etc, which only make sense once, at this true
-- top level. See xmake/engine.lua's own header comment.
includes("xmake/engine.lua")
