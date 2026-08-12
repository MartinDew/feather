-- Reflection-codegen helpers shared between the top-level xmake.lua
-- (run_core_codegen) and module xmake.lua files (e.g. modules/vex_renderer).
--
-- Proper import()-able module rather than plain xmake.lua globals: on_load/
-- before_build/etc scripts run in a sandbox that can't see description-scope
-- globals, only xmake's own APIs and import()ed modules.
--
-- generate_reflection.py parses headers syntactically (stdlib only), so
-- unlike the old clang-backed generator it needs no compiler/include-dir
-- resolution -- just the script path and the dirs to scan.

-- Extensions registered via add_extension() in THIS import() instance --
-- each import() site gets its own fresh module table, so add_extension()
-- and the matching run_*_codegen() call must happen in the same script
-- closure. A caller that can't guarantee that should pass an explicit
-- {extensions = {...}} table to run_*_codegen instead.
local _pending_extensions = {}

local function _resolve_extension(ext)
    -- Relative paths resolve against the CALLING project's root
    -- (os.projectdir()): os.scriptdir() inside an imported module resolves
    -- to the module's own file, not the caller's xmake.lua.
    if type(ext) == "string" then
        return {path = path.absolute(ext, os.projectdir())}
    end
    return {path = path.absolute(ext.path, os.projectdir()), scope = ext.scope}
end

-- Registers a modifier extension (a .py file or dir of them -- see
-- tools/codegen/modifier_api.py) passed to generate_reflection.py as
-- --extension on every subsequent run_*_codegen call from THIS import()
-- instance. Scope defaults to whatever non-core dir(s) that call processes;
-- core is never a default scope, so a project extension can't affect core's
-- output. Pass opts.scope to target a specific dir instead.
--
-- Usage: don't call directly -- pass codegen_extensions to feather_sdk_setup(),
-- which forwards it as run_project_codegen's opts.extensions (keeping
-- registration and the run_*_codegen call in the same closure, as required above).
function add_extension(ext_path, opts)
    opts = opts or {}
    table.insert(_pending_extensions, _resolve_extension({path = ext_path, scope = opts.scope}))
end

-- Builds "--extension <scope>=<path>" argv pairs; an entry without its own
-- scope applies to every directory in default_dirs.
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
-- import() instance, or an explicit list for a caller that'd rather not
-- rely on same-closure state.
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

-- feather_root: defaults to os.projectdir(), correct for the engine's own
-- build but not a downstream project (FeatherSDK.lua passes an explicit root).
-- project_root: tree that generated file ids are computed relative to --
-- the consumer's own root for a consumer build.
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

-- Runs generate_reflection.py for core/ plus any module dirs -- the "refresh
-- everything" entry point, attached to feather.editor/standalone's before_build.
function run_core_codegen(module_dirs, opts)
    opts = opts or {}
    module_dirs = module_dirs or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {}
    for _, dir in ipairs(module_dirs) do
        table.insert(extra, "--module-path")
        table.insert(extra, dir)
    end
    -- Scope defaults to module_dirs only, never core.
    for _, a in ipairs(_extension_argv(extensions, module_dirs)) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, opts.feather_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py")
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end

-- Runs generate_reflection.py for core/ AND a single module dir. For a
-- *module's own* before_build (see modules/vex_renderer/xmake.lua), not the
-- executable's: a module target is a dependency built before its depender's
-- before_build runs, so by the time run_core_codegen would generate core's
-- .gen.h files, the module's files may already be compiling and transitively
-- including headers that need them. Must include core (no --skip-core);
-- write_if_changed makes the redundant pass with run_core_codegen's later
-- pass cheap (0 files changed).
function run_module_codegen(module_dir, opts)
    opts = opts or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {"--module-path", module_dir}
    for _, a in ipairs(_extension_argv(extensions, {module_dir})) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, opts.feather_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    os.vrunv("python3", argv, {curdir = os.projectdir()})
end

-- Runs generate_reflection.py for a downstream "project DLL" repo: only that
-- project's own source dirs, never core. Called from FeatherSDK.lua's
-- before_build. --skip-core is the point of a separate entry point: a
-- consumer must never write into the engine checkout (may be read-only,
-- shared, or on a different commit), so the engine must already be built
-- first -- feather_sdk_setup's on_load check enforces that readably.
--
-- dirs: list of source dirs, each a plain path string, "name=dir" string, or
-- {dir=..., name=...}; name overrides the generated register_<name>_types
-- symbol. The "name=dir" string form exists because set_values() only stores
-- flat scalars. Relative dirs resolve against project_root.
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
    for _, a in ipairs(_extension_argv(extensions, abs_dirs)) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, feather_root, project_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --skip-core (project: %s)", project_root)
    os.vrunv("python3", argv, {curdir = project_root})
end
