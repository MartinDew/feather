-- feather_public_api: public API surface (include dirs + PUBLIC thirdparty
-- packages) for the engine, consumed via add_deps("feather_public_api") by
-- root xmake.lua and by tools/SDK/FeatherSDK.lua for external consumers.
--
-- os.scriptdir(), not os.projectdir(): the latter resolves to the CONSUMER's
-- repo when this file is includes()'d cross-repo from FeatherSDK.lua.
local FEATHER_ROOT = path.directory(os.scriptdir())

target("feather_public_api")
    set_kind("headeronly")
    -- {public = true}: EDITOR_BUILD is transitively visible in a public core
    -- header, so a mismatch between targets would be an ODR bug.
    add_defines("EDITOR_BUILD=" .. (has_config("editor_build") and "1" or "0"), {public = true})
    add_includedirs(FEATHER_ROOT, {public = true})
    add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
    -- xmake doesn't propagate public include dirs across a second headeronly hop.
    add_includedirs(path.join(FEATHER_ROOT, "thirdparty", "SimpleMath"), {public = true})
    add_packages("directxmath", {public = true})
    -- Also direct-linked by binary targets: an object-kind dep's .o files
    -- don't propagate across a second headeronly hop either.
    add_deps("simplemath", {public = true})
    -- flecs/sdl3 link normally on windows/mingw (real shared libs there),
    -- headers-only elsewhere -- see below.
    if is_plat("windows", "mingw") then
        add_packages("flecs", {public = true})
        add_packages("sdl3", {public = true})
    else
        add_packages("flecs", {public = true, links = {}})
        add_packages("sdl3", {public = true, links = {}})
    end
    add_packages("taywee_args", {public = true})
target_end()

-- flecs/sdl3 own process-global state a DLL's own static copy would
-- duplicate uninitialized, so on Linux/macOS the archives are left out and
-- -rdynamic binds to the host exe's copy instead ({links = {}}, not false).
