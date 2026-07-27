-- feather_public_api: single source of truth for the engine's public API
-- surface (public include dirs + PUBLIC thirdparty packages), consumed one
-- hop away via add_deps("feather_public_api"). Used internally by root
-- xmake.lua (modules, executables) AND externally by tools/FeatherSDK.lua
-- for downstream "project DLL" consumers.
--
-- Deliberately uses os.scriptdir() instead of $(projectdir)/os.projectdir():
-- those resolve to whichever project is the top-level build, which is the
-- CONSUMER's repo when this file is includes()'d cross-repo from
-- FeatherSDK.lua. os.scriptdir() is always this file's own physical
-- location, so FEATHER_ROOT is correct regardless of who includes() it.
local FEATHER_ROOT = path.directory(os.scriptdir())

target("feather_public_api")
    set_kind("headeronly")
    add_includedirs(FEATHER_ROOT, {public = true})
    add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
    -- Added directly: xmake doesn't reliably propagate public include dirs
    -- across a headeronly dep boundary (simplemath -> feather_public_api -> exes)
    add_includedirs(path.join(FEATHER_ROOT, "thirdparty", "SimpleMath"), {public = true})
    add_packages("directxmath", {public = true})
    add_deps("simplemath", {public = true})
    add_packages("flecs", {public = true})
    add_packages("taywee_args", {public = true})
    add_packages("sdl3", {public = true})
target_end()
