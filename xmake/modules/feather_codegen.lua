-- Reflection-codegen helpers shared between the top-level xmake.lua
-- (run_codegen, attached to feather.editor/standalone) and module xmake.lua
-- files (e.g. modules/vex_renderer/xmake.lua's own before_build hook).
--
-- This has to be a proper import()-able module rather than a plain global
-- function in xmake.lua: on_load/before_build/etc scripts run inside a sandbox
-- with its own _ENV, and plain Lua globals defined at xmake.lua description
-- scope are NOT visible inside it -- only xmake's own APIs and import()ed
-- modules are. import("feather_codegen") is the supported way to share logic
-- across script-scope closures in different xmake.lua files.
--
-- tools/codegen/generate_reflection.py parses headers syntactically (Python stdlib
-- only) rather than via a clang AST dump, so unlike the old clang-backed
-- generator this needs no compiler resolution and no include-dir collection --
-- just the script path and the directories to scan.

local function common_argv(extra)
    local proj = os.projectdir()
    local argv = {
        path.join(proj, "tools", "codegen", "generate_reflection.py"),
        "--core-path", path.join(proj, "core"),
        "--project-root", proj,
    }
    for _, e in ipairs(extra or {}) do table.insert(argv, e) end
    return argv
end

-- Runs generate_reflection.py for core/ plus, when given, one or more module
-- dirs (vex_renderer today) -- the "refresh everything" entry point, attached
-- to feather.editor/standalone's before_build. module_dirs may be nil/empty.
function run_core_codegen(module_dirs)
    local extra = {}
    for _, dir in ipairs(module_dirs or {}) do
        table.insert(extra, "--module-path")
        table.insert(extra, dir)
    end
    local argv = common_argv(extra)

    cprint("${cyan}[codegen]${reset} generate_reflection.py")
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end

-- Runs generate_reflection.py for core/ AND a single module directory.
-- Intended for a *module's own* before_build (see modules/vex_renderer/xmake.lua),
-- not the executable's: a module target like vex_renderer_standalone is a
-- dependency of feather.standalone, and xmake builds dependencies -- including
-- compiling their files -- before the depending target's own before_build runs.
-- So by the time run_core_codegen (attached to feather.standalone) would
-- generate core/*/*.gen.h and register_vex_renderer_types.gen.cpp, xmake may
-- already be trying to compile the module's files -- which now transitively
-- #include core headers that themselves need their own generated .gen.h (e.g.
-- vex_renderer.h -> rendering/render_scene.h -> resources/material.h). This
-- MUST include core (no --skip-core): a module-only pass leaves core's .gen.h
-- files missing entirely, which is a harder failure than the redundant-parse
-- cost of doing both here -- write_if_changed makes that redundant work with
-- run_core_codegen's own later pass harmless (it just finds 0 files changed;
-- with the syntactic parser this whole redundant pass costs milliseconds).
function run_module_codegen(module_dir)
    local argv = common_argv({"--module-path", module_dir})

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end
