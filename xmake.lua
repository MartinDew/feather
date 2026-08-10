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

-- Release mode only: xmake's mode.release rule does
--     if not target:get("symbols") and target:kind() ~= "shared" then
--         target:set("symbols", "hidden")
--     end
-- (rules/mode/xmake.lua), i.e. -fvisibility=hidden -fvisibility-inlines-hidden
-- on every binary and static target. That silently defeats the -rdynamic on
-- feather.<variant>: a hidden symbol never reaches .dynsym, so nothing the
-- engine defines is visible to a dlopen'd project DLL, and loading one fails
-- outright with e.g. "undefined symbol: _ZN7feather7ClassDB9_instanceE".
-- It has to be set here rather than on the executables, because the objects
-- linked into them are compiled under their own targets (feather_module_target
-- static libs, simplemath) and each needs the same visibility.
--
-- The old CMake build had no visibility preset and set ENABLE_EXPORTS ON, so
-- this restores parity rather than inventing a policy. Guarded on release
-- because setting symbols at all would otherwise pre-empt mode.debug /
-- mode.releasedbg's symbols = "debug" and cost us -g. "none" maps to no flag
-- in gcc.lua's nf_symbol, which is exactly what's wanted -- mode.release's
-- strip = "all" still runs and .dynsym survives it.
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
-- Headeronly umbrella target giving modules engine include dirs and PUBLIC
-- thirdparty headers without a module -> executable circular dep. Also the
-- single source of truth for downstream "project DLL" consumers, via
-- tools/SDK/FeatherSDK.lua -- see xmake/public_api.lua.
includes("xmake/public_api.lua")

-- ---- Core source files ----------------------------------------------------
-- Mirrors FEATHER_CORE_SOURCES in the old CMakeLists.txt exactly.
local CORE_SOURCES = {
    "core/framework/callable.cpp",
    "core/framework/reflected.cpp",
    "core/framework/shared_library.cpp",
    "core/framework/variant.cpp",
    "core/framework/variant_array.cpp",
    "core/main/class_db.cpp",
    "core/main/engine.cpp",
    "core/main/engine_settings.cpp",
    "core/main/feather_main.cpp",
    "core/main/launch_settings.cpp",
    "core/main/project_settings.cpp",
    "core/main/window.cpp",
    "core/main/simulation.cpp",
    "core/main/world_sim.cpp",
    "core/math/math_defs.cpp",
    "core/math/projection.cpp",
    "core/math/transform.cpp",
    "core/rendering/mesh_data.cpp",
    "core/rendering/renderer.cpp",
    "core/rendering/rendering_server.cpp",
    "core/rendering/render_scene.cpp",
    "core/resources/material.cpp",
    "core/resources/material_format_loader.cpp",
    "core/resources/mesh.cpp",
    "core/resources/mesh_format_loader.cpp",
    "core/resources/resource.cpp",
    "core/resources/resource_format_loader.cpp",
    "core/resources/resource_loader.cpp",
    "core/resources/rid.cpp",
    "core/resources/shader.cpp",
    "core/resources/texture.cpp",
    "core/resources/texture_format_loader.cpp",
    "core/resources/extension.cpp",
    "core/resources/extension_format_loader.cpp",
    "core/world/ecs_feature.cpp",
    "core/world/rendering_world_feature.cpp",
    "core/world/math_feature.cpp",
    "core/world/register_core_features.cpp",
    "core/world/core_world_feature.cpp",
    "core/world/components/scene.cpp",
}

local GENERATED_SOURCE = {
   -- Generated by tools/codegen/generate_reflection.py (must exist before first compile)
    "core/framework/register_framework_types.gen.cpp",
    "core/main/register_main_types.gen.cpp",
    "core/math/register_math_types.gen.cpp",
    "core/rendering/register_rendering_types.gen.cpp",
    "core/resources/register_resources_types.gen.cpp",
    "core/world/register_world_types.gen.cpp",
    -- Embedded resources are header-only (raw_resources/*.gen.h), so no .cpp here.
}

-- Runs both codegen scripts before any source file compiles; both use
-- write_if_changed() internally, so repeated runs are cheap. The actual
-- generate_reflection.py invocation lives in xmake/modules/feather_codegen.lua,
-- imported below, rather than as a plain function in this file: on_load/before_build/etc
-- scripts run inside a sandbox with its own _ENV that doesn't see ordinary Lua
-- globals defined at xmake.lua description scope, only xmake's own APIs and
-- import()ed modules -- and modules/vex_renderer/xmake.lua's own before_build
-- hook needs this same logic (see feather_codegen.run_module_codegen and the
-- comment there for why).
local function run_codegen(target)
    import("feather_codegen")
    local proj = os.projectdir()

    -- Modules using FCLASS live outside core/ and need their own --module-path so
    -- the generator scans them too (see process_source_dir() in
    -- generate_reflection.py). This pass is redundant with vex_renderer's own
    -- before_build hook (feather_codegen.run_module_codegen) -- that one exists
    -- for build-ordering correctness, this one keeps `xmake` alone (without a full
    -- rebuild) sufficient to refresh everything. vex_renderer is skipped entirely
    -- on macOS (see modules/vex_renderer/xmake.lua) and gated by enable_vex_renderer
    -- elsewhere, so mirror both checks here rather than feeding the generator a
    -- directory whose FCLASS headers aren't actually being compiled into this build.
    local module_dirs = {}
    if not is_plat("macosx") and has_config("enable_vex_renderer") then
        local vex_dir = path.join(proj, "modules", "vex_renderer")
        if os.isdir(vex_dir) then
            table.insert(module_dirs, vex_dir)
        end
    end
    feather_codegen.run_core_codegen(module_dirs)

    cprint("${cyan}[codegen]${reset} generate_embedded_resources.py")
    os.vrunv("python3", {
        path.join(proj, "tools", "codegen", "generate_embedded_resources.py"),
    }, {curdir = proj})
end

-- Body lives in xmake/modules/feather_flags.lua so tools/SDK/FeatherSDK.lua can
-- import() the exact same flags for downstream project DLLs -- notably
-- -Wno-attributes, which reflection's bare [[get]]/[[method]] attributes need
-- wherever an FCLASS header is compiled.
local function apply_compile_flags(target)
    import("feather_flags")
    feather_flags.apply(target)
end

-- ---- Main executables ---------------------------------------------------
for _, variant in ipairs({"editor", "standalone"}) do
    target("feather." .. variant)
        set_kind("binary")
        set_basename("feather." .. variant)
        set_targetdir("$(builddir)/bin")
        add_files(CORE_SOURCES)
        add_files(GENERATED_SOURCE, {always_added=true})

        add_files("modules/modules.gen.cpp")
        add_includedirs("$(projectdir)", "$(projectdir)/core")

        -- EDITOR_BUILD: PUBLIC on editor so module libs compiled into this exe
        -- see it too, matching CMake (Editor PUBLIC, Standalone PRIVATE).
        if variant == "editor" then
            add_defines("EDITOR_BUILD=1", {public = true})
        else
            add_defines("EDITOR_BUILD=0")
        end

        -- BETA in debug + releasedbg (CMake Development), absent in release
        if is_mode("debug", "releasedbg") then
            add_defines("BETA")
        end
        if is_mode("release") then
            add_defines("PRODUCTION")
        end

        add_deps("feather_public_api")
        add_packages("flecs", "assimp", "sdl3", "taywee_args")

        if is_plat("linux") then
            add_rpathdirs("$ORIGIN/lib", "$ORIGIN/runtime")
            -- Old CMake set ENABLE_EXPORTS ON (-> -rdynamic) on Editor/Standalone
            -- so a runtime-loaded project DLL can resolve engine symbols against
            -- the already-loaded process. Consumer DLLs link against this binary
            -- directly (see tools/SDK/FeatherSDK.lua) rather than a separate shared lib.
            add_ldflags("-rdynamic", {force = true})
        end

        -- Windows analog of the above: MSVC only emits a companion import .lib
        -- for an EXE if it's told to export symbols. xmake has no equivalent of
        -- CMake's WINDOWS_EXPORT_ALL_SYMBOLS for kind="binary" targets, so
        -- mirror XMAKE_MIGRATION.md's documented Phase 1 plan: commit a .def
        -- file (extracted via `dumpbin /EXPORTS build/bin/feather.<variant>.exe`)
        -- per variant and apply it here. NOT YET COMMITTED -- requires a Windows
        -- build to generate; until tools/feather.<variant>.def exists, this is a
        -- no-op guarded by os.isfile() rather than a hard failure on other platforms.
        if is_plat("windows") then
            local def_file = path.join(os.scriptdir(), "tools", "feather." .. variant .. ".def")
            if os.isfile(def_file) then
                add_ldflags("/DEF:" .. def_file, {force = true})
            end
        end

        before_build(run_codegen)

        -- Copy raw_resources/shaders next to the executable after every build.
        -- Rules stack (unlike after_build() closures), so modules can attach
        -- their own deploy rules without disturbing this one — see
        -- xmake/helper.lua for feather_module_target()'s exe_rules option.
        add_rules("feather.deploy_shaders")

        -- on_config, not on_load: toolchain-conditional flags need the
        -- resolved toolchain, which isn't available at load time.
        on_config(apply_compile_flags)
    target_end()
end

-- ---- Modules (auto-discovered; re-opens feather.editor/standalone) ------
-- Must come after the executor targets so feather_module_target() can
-- re-open them to add_deps().
includes("modules/xmake.lua")
