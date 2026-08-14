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

-- Why flecs/sdl3 export without their archives (non-Windows): both own
-- process-global state a DLL's own static copy would duplicate uninitialized
-- and segfault on first use, so the links are left out and -rdynamic binds
-- them to the host exe instead. {links = {}}, not {links = false} (falsy in
-- Lua, silently falls through). Windows keeps the static copies and still
-- carries this bug -- its .def-based import lib isn't committed yet.
