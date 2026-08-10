-- Reflection-codegen helpers shared between xmake/engine.lua (run_codegen,
-- attached to the feather target) and module xmake.lua files (e.g.
-- modules/vex_renderer/xmake.lua's own before_build hook).
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
-- Usage (a game project's own xmake.lua): don't call this directly -- pass
-- codegen_extensions to feather_sdk_setup(), which forwards it as
-- run_project_codegen's opts.extensions. That keeps registration and the
-- run_*_codegen call inside the same script closure, which is required (see
-- the _pending_extensions note above).
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

-- feather_root: the FeatherEngine checkout that owns the generator script and
-- core/. Defaults to os.projectdir(), which is correct for the engine's own
-- build but NOT for a downstream project -- there os.projectdir() is the
-- consumer's repo, so tools/SDK/FeatherSDK.lua passes an explicit root.
-- project_root: the tree that generated file ids are computed relative to
-- (see file_id() in generate_reflection.py); the consumer's own root for a
-- consumer build, so a project header's CURRENT_FILE_ID doesn't depend on
-- where the engine happens to be checked out.
local function common_argv(extra, feather_root, project_root)
    feather_root = feather_root or os.projectdir()
    project_root = project_root or feather_root
    local argv = {
        path.join(feather_root, "tools", "codegen", "generate_reflection.py"),
        "--core-path", path.join(feather_root, "core"),
        "--project-root", project_root,
    }
    for _, e in ipairs(extra or {}) do table.insert(argv, e) end
    return argv
end

-- Runs generate_reflection.py for core/ plus, when given, one or more module
-- dirs (vex_renderer today) -- the "refresh everything" entry point, attached
-- to the feather target's before_build. module_dirs may be nil/empty.
-- opts.extensions: see _resolve_extensions above.
-- opts.feather_root: see common_argv above.
function run_core_codegen(module_dirs, opts)
    opts = opts or {}
    module_dirs = module_dirs or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {"--require-export-macro", "FEATHER_API"}
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
    local argv = common_argv(extra, opts.feather_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py")
    -- opts.feather_root (not a bare os.projectdir()) when given: this function
    -- may run from a before_build includes()'d cross-repo, where
    -- os.projectdir() resolves to the CONSUMER, not the engine. Falls back to
    -- os.projectdir() for a caller (e.g. modules/vex_renderer/xmake.lua today)
    -- that hasn't been made cross-repo-safe yet -- same default common_argv uses.
    os.vrunv("python3", argv, {curdir = opts.feather_root or os.projectdir()})
end

-- Runs generate_reflection.py for core/ AND a single module directory.
-- Intended for a *module's own* before_build (see modules/vex_renderer/xmake.lua),
-- not the executable's: a module target like vex_renderer is a dependency of
-- feather, and xmake builds dependencies -- including compiling their files --
-- before the depending target's own before_build runs. So by the time
-- run_core_codegen (attached to the feather target) would generate core's
-- .gen.h files and register_vex_renderer_types.gen.cpp, xmake may already be
-- trying to compile the module's files -- which now transitively
-- #include core headers that themselves need their own generated .gen.h (e.g.
-- vex_renderer.h -> rendering/render_scene.h -> resources/material.h). This
-- MUST include core (no --skip-core): a module-only pass leaves core's .gen.h
-- files missing entirely, which is a harder failure than the redundant-parse
-- cost of doing both here -- write_if_changed makes that redundant work with
-- run_core_codegen's own later pass harmless (it just finds 0 files changed;
-- with the syntactic parser this whole redundant pass costs milliseconds).
-- opts.extensions: see _resolve_extensions above.
-- opts.feather_root: see common_argv above.
function run_module_codegen(module_dir, opts)
    opts = opts or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {"--require-export-macro", "FEATHER_API", "--module-path", module_dir}
    -- Scope defaults to module_dir only, never core -- same reasoning as
    -- run_core_codegen above.
    for _, a in ipairs(_extension_argv(extensions, {module_dir})) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, opts.feather_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    -- See the matching comment in run_core_codegen above.
    os.vrunv("python3", argv, {curdir = opts.feather_root or os.projectdir()})
end

-- Runs generate_reflection.py for a DOWNSTREAM project ("project DLL") repo:
-- only that project's own source dirs, never core. Called from
-- tools/SDK/FeatherSDK.lua's before_build; see feather_sdk_setup's codegen_dirs.
--
-- --skip-core is the whole point of having a separate entry point from
-- run_module_codegen: the engine's own module targets legitimately regenerate
-- core (their headers transitively include core headers whose .gen.h may not
-- exist yet), but a consumer must never write into someone else's checkout --
-- that checkout may be read-only, shared between projects, or on a different
-- commit. The tradeoff is that the engine must already be built (its core
-- .gen.h files present) before a consumer builds, which feather_sdk_setup's
-- on_load check enforces with a readable message.
--
-- dirs: list of source dirs. Each entry is a plain path string, a
-- "name=dir" string, or {dir = ..., name = ...}; name overrides the generated
-- register_<name>_types symbol (a project's sources usually live in a
-- generically-named "src", which would otherwise give register_src_types).
-- The "name=dir" string form exists because xmake's set_values() only stores
-- flat scalars, so FeatherSDK.lua can't hand a list of tables through a target
-- value into its before_build closure. Relative dirs resolve against project_root.
-- opts.extensions: see _resolve_extensions above.
function run_project_codegen(feather_root, project_root, dirs, opts)
    opts = opts or {}
    local extensions = _resolve_extensions(opts.extensions)

    local abs_dirs = {}
    local extra = {"--skip-core"}
    for _, entry in ipairs(dirs or {}) do
        local d, name
        if type(entry) == "table" then
            d, name = entry.dir, entry.name
        else
            local n, sep, rest = entry:match("^([^=]*)(=?)(.*)$")
            if sep == "=" then
                d, name = rest, n
            else
                d = entry
            end
        end
        local abs = path.absolute(d, project_root)
        table.insert(abs_dirs, abs)
        table.insert(extra, "--module-path")
        table.insert(extra, name and (name .. "=" .. abs) or abs)
    end
    -- Project extensions scope to the project's own dirs only, never core --
    -- core isn't even scanned here, but keep the same invariant as the two
    -- entry points above so the scoping rule reads identically everywhere.
    for _, a in ipairs(_extension_argv(extensions, abs_dirs)) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, feather_root, project_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --skip-core (project: %s)", project_root)
    os.vrunv("python3", argv, {curdir = project_root})
end
