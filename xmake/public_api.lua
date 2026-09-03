-- feather_public_api: public API surface (include dirs + PUBLIC thirdparty
-- packages) for the engine, consumed via add_deps("feather_public_api") by
-- root xmake.lua. Engine-internal: a plugin compiles none of these headers.
--
-- os.scriptdir(), not os.projectdir(): the latter resolves to the CONSUMER's
-- repo if this file were ever includes()'d cross-repo.
local FEATHER_ROOT = path.directory(os.scriptdir())

target("feather_public_api")
    set_kind("headeronly")
    -- {public = true}: EDITOR_BUILD is transitively visible in a public core
    -- header, so a mismatch between targets would be an ODR bug.
    add_defines("EDITOR_BUILD=" .. (has_config("editor_build") and "1" or "0"), {public = true})
    add_includedirs(FEATHER_ROOT, {public = true})
    add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
    -- xmake doesn't propagate public include dirs across a second headeronly hop.
    add_includedirs(path.join(FEATHER_ROOT, "tools", "SDK", "thirdparty", "SimpleMath"), {public = true})
    add_packages("directxmath", {public = true})
    -- Also direct-linked by binary targets: an object-kind dep's .o files
    -- don't propagate across a second headeronly hop either.
    add_deps("simplemath", {public = true})
    -- flecs is a real shared library on every platform now (see
    -- thirdparty/xmake.lua), so it links normally everywhere: one copy in the
    -- process, and a consumer's undefined flecs symbols resolve against it
    -- through DT_NEEDED rather than hoping the host exported them.
    add_packages("flecs", {public = true})
    -- sdl3 still follows the old arrangement off Windows: headers only, with
    -- the host executable's copy bound at dlopen time -- see below.
    if is_plat("windows", "mingw") then
        add_packages("sdl3", {public = true})
    else
        add_packages("sdl3", {public = true, links = {}})
    end
    add_packages("taywee_args", {public = true})
target_end()

-- sdl3 owns process-global state a DLL's own static copy would duplicate
-- uninitialized, so on Linux/macOS the archive is left out and -rdynamic binds
-- to the host exe's copy instead ({links = {}}, not false).
--
-- That arrangement only works for symbols the host actually exports, which
-- rules it out for a package built with hidden visibility -- the reason flecs
-- above is shared everywhere and linked normally instead.
