-- C bindings for the public API. mrbind_gen_c turns build/bindings/api.json
-- (see xmake/bindings.lua) into C headers a consumer includes, plus C++
-- implementation files that call into the engine.
--
-- Those implementation files compile into the engine binary itself. They are
-- engine code: they are the C-callable face of the same singletons the C++ API
-- exposes, so they belong in the process that owns those singletons rather than
-- in a second artifact that has to find its way back to it.
--
-- The engine exports them the same way it exports every other API symbol -- via
-- -rdynamic on ELF, and via the import library the linker already writes next to
-- feather.exe on Windows (xmake/engine.lua). A C or C# plugin therefore resolves
-- feather_* the way a C++ plugin resolves engine symbols: one mechanism, one
-- artifact, nothing for the engine to locate and load at startup.
--
-- Compiled into the executable rather than linked as a static library on
-- purpose: nothing in the engine references a generated feather_c_* symbol, so
-- a static archive's members would all be dropped as unreferenced, and keeping
-- them would mean --whole-archive/-force_load//WHOLEARCHIVE spelled per
-- platform. The engine already declines to route its own core through a static
-- library for kindred reasons (see xmake/engine.lua).

if not has_config("enable_c_bindings") then
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "c")

-- A rule, not an on_config on the engine target: on_config is a setter, so a second one here would silently replace the engine's
-- own (which applies its compile flags). Rules compose.
rule("feather.c_bindings")
    on_config(function (target)
        import("feather_bindings")

        local sources = feather_bindings.run_gen_c(target, {
            header_dir = path.join(OUTPUT_DIR, "include"),
            source_dir = path.join(OUTPUT_DIR, "src"),
            feather_root = FEATHER_ROOT,
        })

        -- Generated code, and a lot of it -- the engine's warning settings have nothing to act on there. Per-file rather than
        -- per-target: these share a target with the engine's own sources now, which stay warned about.
        local no_warnings = target:has_tool("cxx", "cl", "clang_cl") and "/w" or "-w"
        for _, src in ipairs(sources) do
            -- always_added: none of these exist on disk when the target is first
            -- loaded, only once the generation above has run.
            target:add("files", src, {always_added = true, cxflags = no_warnings})
        end
    end)
rule_end()

target("feather")
    add_rules("feather.c_bindings")

    -- bindings_api's on_config runs the parse the rule above generates from, and on_config follows dependency order
    -- (unlike before_build, which does not -- both confirmed empirically).
    add_deps("bindings_api")
    -- run_gen_c resolves the generator binary through this package handle.
    add_packages("mrbind")

    -- Marks this as the build of the generated bindings themselves, so FEATHER_C_API resolves to dllexport rather than dllimport
    -- (feather_helpers/exports.h) -- a separate macro from the engine's own FEATHER_C_API (run_gen_c()'s prefix comment), though both resolve to "export" here since both are built here.
    add_defines("FEATHER_C_BUILD_LIBRARY")

    -- The generated .cpp files include their sibling generated header by quoted
    -- path, and it lives in include/ rather than next to them.
    add_includedirs(path.join(OUTPUT_DIR, "include"))
    -- mrbind_gen_c writes its internal helper header (__mrbind_c_details.h)
    -- into the source dir, not the header dir -- implementation detail only.
    add_includedirs(path.join(OUTPUT_DIR, "src"))
target_end()
