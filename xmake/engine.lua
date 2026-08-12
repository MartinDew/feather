-- xmake/engine.lua: engine target declarations (the feather executable, its
-- core sources, codegen wiring, and modules). Split out of the root
-- xmake.lua so it can eventually be includes()'d cross-repo -- e.g. by a
-- consumer building a fully static shipping executable (see
-- tools/SDK/FeatherSDK.lua) -- without dragging in set_project()/set_version()/
-- etc, which only make sense once, at the true top level. This file, by
-- itself, is NOT yet sufficient for that (thirdparty packages and
-- feather_public_api must already be includes()'d by the caller, same as the
-- root xmake.lua does today) -- it only owns the target declarations.
--
-- Rooted at os.scriptdir() rather than $(projectdir)/os.projectdir(), same
-- reasoning as xmake/public_api.lua: those resolve to whichever project is
-- top-level for the current invocation, which is the CONSUMER's repo when
-- this file is includes()'d cross-repo. FEATHER_ROOT is captured once here as
-- a description-scope local and closed over by every callback below
-- (before_build/on_config) -- verified directly on this box's xmake (3.1.0)
-- that a real Lua closure keeps such an upvalue intact inside a sandboxed
-- callback, even across a cross-repo includes(); it is only a BARE
-- os.projectdir()/os.scriptdir() call re-evaluated fresh *inside* the
-- callback that resolves against the wrong project. See also
-- xmake/helper.lua and xmake/modules/feather_codegen.lua, which needed the
-- same fix.
local FEATHER_ROOT = path.directory(os.scriptdir())

-- CORE_SOURCES, GENERATED_SOURCE, run_core_source_codegen, and
-- apply_core_compile_flags: shared with a static shipping build's own
-- executable target (feather_game_target(), tools/SDK/FeatherSDK.lua) --
-- see that file's header comment for why this had to move out of this file.
includes(path.join(FEATHER_ROOT, "xmake", "core_sources.lua"))

-- ---- Main executable ------------------------------------------------------
-- One binary (was feather.editor/feather.standalone): EDITOR_BUILD was a full
-- build-variant split for what turns out to be a runtime flag -- editor mode
-- is just Engine::is_editor() reading --editor (see core/main/engine.cpp and
-- core/main/launch_settings.h). Collapsing to one target halves the module
-- build matrix and means a plugin builds once, not once per variant.
target("feather")
    set_kind("binary")
    set_basename("feather")
    set_targetdir("$(builddir)/bin")
    add_files(CORE_SOURCES)
    add_files(GENERATED_SOURCE, {always_added=true})

    add_files(path.join(FEATHER_ROOT, "modules", "modules.gen.cpp"))
    add_includedirs(FEATHER_ROOT, path.join(FEATHER_ROOT, "core"))

    -- See core/framework/feather_api.h: without this, FEATHER_API resolves to
    -- dllimport even here, in the translation units that DEFINE the surface.
    -- On MSVC/clang-cl that's a hard error the moment it hits a static data
    -- member definition (e.g. class_db.cpp's FSINGLETON_INSTANCE(ClassDB) --
    -- "definition of dllimport static field not allowed"); for plain function
    -- definitions the compiler silently promotes dllimport to dllexport with
    -- just a warning, which is how this went unnoticed until a static field
    -- actually hit it. GCC/Clang's visibility("default") branch doesn't care.
    add_defines("FEATHER_BUILDING_CORE")

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
        --
        -- TEMPORARY: this export-everything model is being replaced by a
        -- declared FEATHER_API surface (see the plugin-abi-rework plan) --
        -- this block goes away once that lands.
        add_ldflags("-rdynamic", {force = true})
    end

    -- Windows analog of the above: MSVC only emits a companion import .lib
    -- for an EXE if it's told to export symbols. The old plan here was a
    -- committed .def file (dumpbin /EXPORTS'd from an already-built exe --
    -- chicken-and-egg, never actually landed). That's unnecessary now: the
    -- declared FEATHER_API surface (feather_api.h) already puts
    -- __declspec(dllexport) on every exported class/function when
    -- FEATHER_BUILDING_CORE is set (this target), which is sufficient on its
    -- own for MSVC/clang-cl to emit build/bin/feather.lib alongside
    -- feather.exe -- no .def, no ldflags, no Windows-only block needed here
    -- at all. See tools/SDK/FeatherSDK.lua, which links against exactly that
    -- generated .lib.

    before_build(run_core_source_codegen)

    -- Copy raw_resources/shaders next to the executable after every build.
    -- Rules stack (unlike after_build() closures), so modules can attach
    -- their own deploy rules without disturbing this one — see
    -- xmake/helper.lua for feather_module_target()'s exe_rules option.
    add_rules("feather.deploy_shaders")

    -- on_config, not on_load: toolchain-conditional flags need the
    -- resolved toolchain, which isn't available at load time.
    on_config(apply_core_compile_flags)
target_end()

-- ---- Modules (auto-discovered; re-opens the feather target) -------------
-- Must come after the executable target so feather_module_target() can
-- re-open it to add_deps(). Joined against FEATHER_ROOT (not a bare
-- relative path) for the same cross-repo-includes() reason as everything
-- else in this file.
includes(path.join(FEATHER_ROOT, "modules", "xmake.lua"))
