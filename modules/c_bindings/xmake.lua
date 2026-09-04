-- The engine's exported C ABI: mrbind_gen_c turns build/bindings/api.json (see
-- xmake/bindings.lua) into C headers a consumer includes plus C++ glue calling
-- into the engine, and scripted_abi.cpp adds the hand-written ECS entry points
-- that no parse can express.
--
-- All of it is engine code: it is the C-callable face of the same singletons
-- the C++ API exposes, so it belongs in the process that owns them rather than
-- in a second artifact that has to find its way back. The engine exports it the
-- way it exports every other API symbol -- -rdynamic on ELF, the import library
-- the linker writes beside feather.exe on Windows (xmake/engine.lua).
--
-- An object module, not a static one: nothing in the engine references a
-- generated feather_c_* symbol, so a static archive's members would all be
-- dropped as unreferenced and keeping them would mean --whole-archive /
-- -force_load / /WHOLEARCHIVE spelled per platform.

if not has_config("enable_c_bindings") then
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "c")

-- bindings_api's on_config runs the parse this generates from, and on_config follows dependency order (unlike before_build, which does
-- not -- both confirmed empirically).
feather_module_target("c_bindings", os.scriptdir(), {"scripted_abi.cpp"},
    {kind = "object", deps = {"bindings_api"}})

target("c_bindings")
    -- run_gen_c resolves the generator binary and DirectXMath's include dir
    -- through these handles; a package reached only via a dep isn't in pkg().
    add_packages("mrbind", "directxmath")

    -- Marks this as the build of the generated bindings themselves, so FEATHER_C_API resolves to dllexport rather than dllimport
    -- (feather_helpers/exports.h) -- a separate macro from the engine's own, though both resolve to "export" here since both are built here.
    add_defines("FEATHER_C_BUILD_LIBRARY")

    -- The generated .cpp files include their sibling generated header by quoted path, and it lives in include/ rather than next to them;
    -- mrbind_gen_c writes __mrbind_c_details.h into the source dir instead.
    add_includedirs(path.join(OUTPUT_DIR, "include"), path.join(OUTPUT_DIR, "src"))

    on_config(function (target)
        import("feather_bindings")

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
    end)
target_end()
