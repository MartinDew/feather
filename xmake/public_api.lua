-- feather_public_api: single source of truth for the engine's public API
-- surface (public include dirs + PUBLIC thirdparty packages), consumed one
-- hop away via add_deps("feather_public_api"). Used internally by root
-- xmake.lua (modules, executables) AND externally by tools/SDK/FeatherSDK.lua
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
    -- flecs and sdl3 are exported HEADERS ONLY off Windows -- see the block
    -- below the target for why. taywee_args and directxmath are headeronly
    -- packages anyway, so they need no special handling.
    if is_plat("windows") then
        add_packages("flecs", {public = true})
        add_packages("sdl3", {public = true})
    else
        add_packages("flecs", {public = true, links = {}})
        add_packages("sdl3", {public = true, links = {}})
    end
    add_packages("taywee_args", {public = true})
target_end()

-- Why flecs/sdl3 are exported without their archives (non-Windows):
--
-- Both own process-global mutable state -- flecs's ecs_os_api and its builtin
-- component-id globals, SDL's subsystem refcounts and event queue -- that the
-- engine EXECUTABLE initializes and every dlopen'd project DLL must share. If a
-- DLL links its own static copy it gets a second, never-initialized ecs_os_api
-- and segfaults on a NULL function pointer the first time it imports an ECS
-- module (flecs::_::import<T> -> WorldSim::init -> Engine::run). That was the
-- actual crash: nm -D on libexample.so showed 625 DEFINED and 0 undefined ecs_
-- symbols, i.e. nothing for the loader to unify.
--
-- Leaving the links out makes those symbols undefined in the DLL, so the loader
-- binds them to the already-loaded host executable, which exports them via
-- add_ldflags("-rdynamic") in the root xmake.lua. Nothing engine-side regresses:
-- module targets are static libs (ar collects objects, it links nothing), and
-- feather.editor/feather.standalone still pull the real archives through their
-- own add_packages("flecs", "assimp", "sdl3", "taywee_args").
--
-- {links = {}}, NOT {links = false}: xmake only honours a per-target package
-- override when the value is truthy (core/project/target.lua, the
-- `elseif configinfo and configinfo[name]` branch), and false is falsy in Lua,
-- so it silently falls through to the package's own links. An empty table is
-- truthy and yields no links.
--
-- Windows keeps the static copies: a DLL there cannot have unresolved imports,
-- and the import library it would need comes from tools/feather.<variant>.def,
-- which is not committed yet (see the root xmake.lua's is_plat("windows")
-- block). So the Windows consumer path carries this same latent bug for now.
