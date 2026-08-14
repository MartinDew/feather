-- feather_public_api: single source of truth for the engine's own public API
-- surface (public include dirs + PUBLIC thirdparty packages), consumed one
-- hop away via add_deps("feather_public_api") by the engine executable and
-- in-engine modules (e.g. modules/vex_renderer). A plugin is no longer a
-- consumer of this at all -- it only ever sees core/extension/feather_interface.h,
-- a pure C header with no engine dependency behind it (docs/plugin_abi.md).
local FEATHER_ROOT = path.directory(os.scriptdir())

target("feather_public_api")
    set_kind("headeronly")
    add_defines("EDITOR_BUILD=" .. (has_config("editor_build") and "1" or "0"), {public = true})
    add_includedirs(FEATHER_ROOT, {public = true})
    add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
    -- Added directly: xmake doesn't reliably propagate public include dirs
    -- across a headeronly dep boundary (simplemath -> feather_public_api -> exes)
    add_includedirs(path.join(FEATHER_ROOT, "thirdparty", "SimpleMath"), {public = true})
    add_packages("directxmath", {public = true})
    add_deps("simplemath", {public = true})
    add_packages("flecs", "sdl3", {public = true})
    add_packages("taywee_args", {public = true})
target_end()
