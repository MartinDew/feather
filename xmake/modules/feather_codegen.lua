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

-- Extensions registered via add_extension() in THIS import() instance --
-- xmake gives each import() site its own fresh module table, so this list
-- does NOT carry state across different xmake.lua files. add_extension() and
-- the matching run_*_codegen() call must happen in the same script closure
-- (see add_extension's own doc below); a caller that can't guarantee that
-- should pass an explicit {extensions = {...}} table to run_*_codegen instead.
local _pending_extensions = {}

local function _resolve_extension(ext)
    -- ext is either a bare path string (scope defaults to whatever dir(s)
    -- the run_*_codegen call actually processes) or a {path=..., scope=...}
    -- table (an explicit scope dir, overriding that default). A relative
    -- path is resolved against the CALLING project's root (os.projectdir()),
    -- not this script's own directory -- os.scriptdir() inside an imported
    -- module resolves to the module's own file, not the caller's xmake.lua,
    -- so it can't be used for this.
    if type(ext) == "string" then
        return {path = path.absolute(ext, os.projectdir())}
    end
    return {path = path.absolute(ext.path, os.projectdir()), scope = ext.scope}
end

-- Registers a modifier extension (a .py file or a directory of them --
-- see tools/codegen/modifier_api.py) to be passed to generate_reflection.py
-- as --extension on every subsequent run_core_codegen/run_module_codegen call
-- from THIS import() instance. Scope (which directory the extension applies
-- to) defaults to whatever non-core dir(s) that run_*_codegen call itself
-- processes -- core/ is deliberately never a default scope, so a project
-- extension can never change what core/ generates regardless of which target
-- happens to trigger the codegen pass (see the module docstring in
-- modifier_api.py for why that invariant matters). Pass opts.scope to target
-- a specific directory instead (e.g. from a project with more than one
-- codegen'd source tree).
--
-- Usage (a game project's own xmake.lua):
--   import("feather_codegen")
--   feather_codegen.add_extension("codegen/rpg_modifiers.py")
--   feather_codegen.run_module_codegen(os.scriptdir())
function add_extension(ext_path, opts)
    opts = opts or {}
    table.insert(_pending_extensions, _resolve_extension({path = ext_path, scope = opts.scope}))
end

-- Builds "--extension <scope>=<path>" argv pairs for `extensions` (a list in
-- the same shape as _pending_extensions), where an entry without its own
-- scope applies to every directory in `default_dirs`.
local function _extension_argv(extensions, default_dirs)
    local argv = {}
    for _, ext in ipairs(extensions) do
        local scopes = ext.scope and {ext.scope} or default_dirs
        for _, dir in ipairs(scopes) do
            table.insert(argv, "--extension")
            table.insert(argv, dir .. "=" .. ext.path)
        end
    end
    return argv
end

-- extensions_opt: nil to use whatever add_extension() recorded in this
-- import() instance, or an explicit list (same shape add_extension() takes:
-- a bare path string, or {path=..., scope=...}) for a caller that would
-- rather not rely on same-closure state.
local function _resolve_extensions(extensions_opt)
    if extensions_opt == nil then
        return _pending_extensions
    end
    local out = {}
    for _, ext in ipairs(extensions_opt) do
        table.insert(out, _resolve_extension(ext))
    end
    return out
end

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
-- opts.extensions: see _resolve_extensions above.
function run_core_codegen(module_dirs, opts)
    opts = opts or {}
    module_dirs = module_dirs or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {}
    for _, dir in ipairs(module_dirs) do
        table.insert(extra, "--module-path")
        table.insert(extra, dir)
    end
    -- Scope defaults to module_dirs only, NEVER core -- see add_extension's
    -- doc for why core's generated output must not depend on which
    -- extensions happen to be registered.
    for _, a in ipairs(_extension_argv(extensions, module_dirs)) do
        table.insert(extra, a)
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
-- opts.extensions: see _resolve_extensions above.
function run_module_codegen(module_dir, opts)
    opts = opts or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {"--module-path", module_dir}
    -- Scope defaults to module_dir only, never core -- same reasoning as
    -- run_core_codegen above.
    for _, a in ipairs(_extension_argv(extensions, {module_dir})) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end
