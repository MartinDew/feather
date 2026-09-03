-- The one MRBind parse the whole build shares. Every language binding is
-- generated from its output, so the parse happens once, here, rather than once
-- per language: see modules/{c,cs,py}_bindings/xmake.lua for the consumers.
--
-- Deliberately on_config rather than before_build. before_build hooks run in
-- parallel across targets and are NOT ordered by target dependencies (both
-- confirmed empirically), so a generator's before_build could run before -- or
-- at the same time as -- this parse. on_config is serial and does follow
-- dependency order, so every target that add_deps("bindings_api") is
-- guaranteed a finished, up-to-date api.json by the time its own on_config
-- runs. It's also the earliest point where compile flags can be resolved at
-- all (compiler.load needs the target's toolchain, which on_load doesn't have).
--
-- os.scriptdir(), not os.projectdir(): the latter would resolve to a
-- CONSUMER's repo if this file were ever includes()'d cross-repo.
local FEATHER_ROOT = path.directory(os.scriptdir())

-- The public API surface that gets bindings: core's headers. Modules are
-- deliberately out -- they're implementation, reachable through core's
-- interfaces.
local API_DIRS = {path.join(FEATHER_ROOT, "core")}

rule("feather.mrbind_api")
    on_config(function (target)
        import("feather_bindings")
        import("feather_codegen")

        local outputs = {feather_bindings.api_json_path()}
        if has_config("enable_py_bindings") then
            table.insert(outputs, feather_bindings.api_macros_path())
        end

        -- Reflection codegen first. Core's headers #include their own
        -- "<name>.gen.h", which doesn't exist on a fresh checkout until this
        -- runs -- and the feather target's own before_build codegen is much
        -- too late for a parse happening at config time.
        local module_dirs = {}
        if not is_plat("macosx") and has_config("enable_vex_renderer") then
            local vex_dir = path.join(FEATHER_ROOT, "modules", "vex_renderer")
            if os.isdir(vex_dir) then
                table.insert(module_dirs, vex_dir)
            end
        end
        feather_codegen.run_core_codegen(module_dirs, {feather_root = FEATHER_ROOT})

        -- Absolute: autogendir() is relative to the project directory, and the
        -- parser's macro output embeds this path in an #include of its own.
        local combined_header, header_list_changed = feather_bindings.generate_combined_header(
            path.absolute(path.join(target:autogendir(), "combined_input.h")), FEATHER_ROOT, API_DIRS)

        -- A changed header list means a header was added or removed, which an
        -- mtime comparison alone can miss (removing one leaves every remaining
        -- file untouched).
        if not header_list_changed
                and not feather_bindings.parser_flags_changed()
                and not feather_bindings.outputs_are_stale(outputs, API_DIRS) then
            return
        end

        feather_bindings.run_parse(target, {
            combined_header = combined_header,
            output = feather_bindings.api_json_path(),
            format = "json",
            -- The C ABI binds SimpleMath's math types; the Python parse below
            -- does not. See feather_bindings.c_abi_parser_flags.
            extra_parser_flags = feather_bindings.c_abi_parser_flags(),
        })

        -- The Python backend needs the same parse in mrbind's macro format --
        -- it has no generator binary and compiles the parse output directly
        -- (see modules/py_bindings/xmake.lua).
        if has_config("enable_py_bindings") then
            feather_bindings.run_parse(target, {
                combined_header = combined_header,
                output = feather_bindings.api_macros_path(),
                format = "macros",
                -- A separate parse anyway, so the handful of things only
                -- pybind11 objects to are dropped here rather than from the
                -- shared set the C and C# bindings read.
                extra_parser_flags = feather_bindings.python_parser_flags(),
            })
        end

        -- Last, so an interrupted or failed parse doesn't leave a stamp
        -- claiming the outputs match the current flags.
        feather_bindings.write_parser_flags_stamp()
    end)
rule_end()

-- Phony: it produces files, not a linkable artifact. Everything generating
-- bindings depends on it, which is what puts its parse ahead of their
-- generation steps.
target("bindings_api")
    set_kind("phony")
    set_group("bindings")

    -- Engine include dirs and public thirdparty headers -- the parse has to
    -- see core's headers exactly as the engine compiles them.
    add_deps("feather_public_api")
    add_packages("mrbind")
    -- Windows-only: mrbind is built against libllvm's clang there rather than
    -- any system clang (see thirdparty/xmake.lua), and resolving that clang
    -- needs a direct target:pkg("libllvm") handle.
    if is_plat("windows") then
        add_packages("libllvm")
    end

    -- Same defines the engine's own targets set (xmake/engine.lua,
    -- feather_module_target); without them the parse sees a different API than
    -- the one that gets compiled, since #if BETA members would disappear.
    if is_mode("debug", "releasedbg") then
        add_defines("BETA")
    end
    if is_mode("release") then
        add_defines("PRODUCTION")
    end

    add_rules("feather.mrbind_api")
target_end()
