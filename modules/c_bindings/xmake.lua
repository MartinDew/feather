-- C bindings for the public API. mrbind_gen_c turns build/bindings/api.json
-- (see xmake/bindings.lua) into C headers a consumer includes, plus C++
-- implementation files that call into the engine.
--
-- Those implementation files build into a shared library rather than into the
-- engine binary, because a consumer needs something it can actually load: a C
-- program links it, and the generated C# bindings P/Invoke it by name (see
-- modules/cs_bindings/xmake.lua). It resolves engine symbols the same way a
-- project DLL does -- against the engine's import library on Windows, and
-- against the host process at load time everywhere else (see
-- tools/SDK/FeatherSDK.lua, which sets up consumer DLLs the same way). It is
-- therefore loadable inside a running engine process, not standalone.

if not has_config("enable_c_bindings") then
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "c")
local ENGINE_BIN_DIR = path.join(FEATHER_ROOT, "build", "bin")

target("c_bindings")
    set_kind("shared")
    -- The name consumers link and the C# bindings load: libfeather_c.so,
    -- feather_c.dll, libfeather_c.dylib.
    set_basename("feather_c")
    set_group("bindings")
    -- Everything a C consumer needs ends up under build/bindings/c: headers in
    -- include/, the library here.
    set_targetdir(path.join(OUTPUT_DIR, "lib"))
    -- Generated code, and a lot of it -- the engine's own warning settings
    -- have nothing to say about it.
    set_warnings("none")

    -- bindings_api's on_config runs the parse this target generates from, and
    -- on_config follows dependency order (unlike before_build, which does not
    -- -- both confirmed empirically).
    add_deps("bindings_api")
    add_deps("feather_public_api")
    -- Direct, not just via feather_public_api: see xmake/public_api.lua.
    add_deps("simplemath")
    -- Ordering only ({inherit = false}: an executable has no link flags to
    -- pass on). On Windows the link below needs the engine's import library,
    -- which doesn't exist until the engine has been linked.
    add_deps("feather", {inherit = false})

    -- Only this target's own generation step needs the generator binaries.
    add_packages("mrbind")

    -- Its own compiled copy of the global operator new/delete overrides, so
    -- allocations made inside the bindings route through the engine's heap
    -- rather than this library's own -- exactly what feather_sdk_setup() does
    -- for a consumer DLL.
    add_files(path.join(FEATHER_ROOT, "core", "framework", "alloc.cpp"))

    -- Marks this as the build of the generated library itself, so its own
    -- FEATHER_C_API resolves to dllexport rather than dllimport (see the
    -- generated feather_helpers/exports.h). Distinct from the engine's
    -- FEATHER_API on purpose -- see run_gen_c()'s macro-prefix comment.
    add_defines("FEATHER_C_BUILD_LIBRARY")

    -- Not FEATHER_BUILDING_ENGINE: this is a consumer of the engine's exported
    -- API, so FEATHER_API has to resolve to dllimport here. The mode defines
    -- must match the engine's, or headers compiled into both disagree about
    -- #if BETA members.
    if is_mode("debug", "releasedbg") then
        add_defines("BETA")
    end
    if is_mode("release") then
        add_defines("PRODUCTION")
    end

    -- The generated .cpp files include their sibling generated header by
    -- quoted path, and it lives in include/ rather than next to them. Public:
    -- a consumer target needs the same include dir.
    add_includedirs(path.join(OUTPUT_DIR, "include"), {public = true})
    -- mrbind_gen_c writes its internal helper header (__mrbind_c_details.h)
    -- into the source dir, not the header dir -- implementation detail only.
    add_includedirs(path.join(OUTPUT_DIR, "src"), {public = false})

    if is_plat("windows", "mingw") then
        -- mingw is a distinct is_plat() from "windows" but takes the same
        -- link-time path, not the ELF branch's load-time binding.
        add_linkdirs(ENGINE_BIN_DIR)
        add_links("feather")
        if is_plat("mingw") then
            add_shflags("-static-libgcc", "-static-libstdc++", {force = true})
            add_syslinks("stdc++exp")
        end
    elseif is_plat("macosx") then
        -- Mach-O rejects undefined symbols in a dylib by default.
        add_shflags("-undefined", "dynamic_lookup", {force = true})
    end
    -- ELF: engine symbols stay undefined here and bind to the host executable
    -- when the library is loaded (the engine links with -rdynamic).

    on_config(function (target)
        import("feather_bindings")
        import("feather_flags")

        local sources = feather_bindings.run_gen_c(target, {
            header_dir = path.join(OUTPUT_DIR, "include"),
            source_dir = path.join(OUTPUT_DIR, "src"),
            feather_root = FEATHER_ROOT,
        })
        for _, src in ipairs(sources) do
            -- always_added: none of these exist on disk when the target is
            -- first loaded, only once the generation above has run.
            target:add("files", src, {always_added = true})
        end

        -- Same LTO/sanitizer/optimization flags the engine compiles itself
        -- with -- those have to match across the boundary. Its warning flags
        -- come along with them, so silence warnings again afterwards: there's
        -- nothing to act on in generated code, and there is a lot of it.
        feather_flags.apply(target)
        target:add("cxflags", target:has_tool("cxx", "cl", "clang_cl") and "/w" or "-w", {force = true})
    end)
target_end()
