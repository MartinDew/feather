-- feather_public_api: single source of truth for the engine's public API
-- surface (public include dirs + PUBLIC thirdparty packages), consumed one
-- hop away via add_deps("feather_public_api"). Used internally by root
-- xmake.lua and externally by tools/SDK/FeatherSDK.lua for consumers.
--
-- os.scriptdir(), not os.projectdir(): the latter resolves to the CONSUMER's
-- repo when this file is includes()'d cross-repo from FeatherSDK.lua.
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
    -- flecs/sdl3 export headers only off Windows -- see below.
    if is_plat("windows") then
        add_packages("flecs", {public = true})
        add_packages("sdl3", {public = true})
    else
        add_packages("flecs", {public = true, links = {}})
        add_packages("sdl3", {public = true, links = {}})
    end
    add_packages("taywee_args", {public = true})
target_end()

-- Why flecs/sdl3 are exported without their archives (non-Windows): both own
-- process-global mutable state (flecs's ecs_os_api and component-id globals,
-- SDL's subsystem refcounts/event queue) that the engine executable
-- initializes and every dlopen'd project DLL must share. A DLL linking its
-- own static copy gets a second, never-initialized ecs_os_api and segfaults
-- on a NULL function pointer on first ECS module import (this was an actual
-- crash: nm -D on libexample.so showed 625 defined, 0 undefined ecs_ symbols
-- -- nothing for the loader to unify). Leaving the links out makes the
-- symbols undefined in the DLL, so the loader binds them to the host exe,
-- which exports them via -rdynamic (root xmake.lua). Module targets are
-- static libs so this doesn't affect them; feather.editor/standalone still
-- pull the real archives via their own add_packages().
--
-- {links = {}}, not {links = false}: xmake only honours a per-target package
-- override when truthy, and false is falsy in Lua -- it'd silently fall
-- through to the package's own links.
--
-- Windows keeps the static copies (a DLL there can't have unresolved
-- imports, and the import lib would need tools/feather.<variant>.def, not
-- yet committed), so the Windows consumer path still carries this bug.
