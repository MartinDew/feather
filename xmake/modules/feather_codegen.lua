-- Reflection-codegen helpers shared between the top-level xmake.lua
-- (run_core_codegen) and module xmake.lua files. Must be an import()-able
-- module, not plain xmake.lua globals: on_load/before_build sandboxes can't
-- see description-scope globals.

-- Per-import()-instance list; add_extension() and its matching
-- run_*_codegen() call must happen in the same script closure. Pass an
-- explicit {extensions = {...}} to run_*_codegen otherwise.
local _pending_extensions = {}

local function _resolve_extension(ext)
    -- Relative paths resolve against the caller's root: os.scriptdir()
    -- inside an imported module resolves to this file, not the caller's.
    if type(ext) == "string" then
        return {path = path.absolute(ext, os.projectdir())}
    end
    return {path = path.absolute(ext.path, os.projectdir()), scope = ext.scope}
end

-- Registers a modifier extension (see tools/codegen/modifier_api.py), scoped
-- to whatever non-core dir(s) the next run_*_codegen call processes (core is
-- never a default scope). Don't call directly -- pass codegen_extensions to
-- feather_sdk_setup() instead, to keep this in the same closure as required above.
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

-- feather_root defaults to os.projectdir(); FeatherSDK.lua passes an
-- explicit one for a downstream project.
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
-- everything" entry point, attached to the feather target's before_build.
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
    -- opts.feather_root (not a bare os.projectdir()) when given: this function
    -- may run from a before_build includes()'d cross-repo, where
    -- os.projectdir() resolves to the CONSUMER, not the engine. Falls back to
    -- os.projectdir() for a caller that hasn't passed one -- same default
    -- common_argv uses.
    os.vrunv("python3", argv, {curdir = opts.feather_root or os.projectdir()})
end

-- Runs generate_reflection.py for core/ AND a single module dir -- for a
-- *module's own* before_build, since it builds before the depending exe's
-- before_build (run_core_codegen) would otherwise generate core's .gen.h
-- first. write_if_changed makes the redundant later pass cheap.
function run_module_codegen(module_dir, opts)
    opts = opts or {}
    local extensions = _resolve_extensions(opts.extensions)

    local extra = {"--module-path", module_dir}
    for _, a in ipairs(_extension_argv(extensions, {module_dir})) do
        table.insert(extra, a)
    end
    local argv = common_argv(extra, opts.feather_root)

    cprint("${cyan}[codegen]${reset} generate_reflection.py --module-path %s", module_dir)
    -- See the matching comment in run_core_codegen above.
    os.vrunv("python3", argv, {curdir = opts.feather_root or os.projectdir()})
end

-- Runs generate_reflection.py for a downstream "project DLL" repo: only its
-- own source dirs, never core (--skip-core) -- a consumer must never write
-- into the engine checkout, so the engine must already be built first.
--
-- dirs: plain path strings, "name=dir" strings, or {dir=..., name=...};
-- name overrides the generated register_<name>_types symbol.
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

-- Runs the engine's --dump-api then generate_bindings.py (see
-- docs/plugin_abi.md), producing <out_dir>/feather_bindings.gen.h for a
-- plugin project. Both steps are mtime-guarded against the engine binary,
-- so a no-change build costs one process spawn plus a cheap write_if_changed.
function run_binding_codegen(feather_root, engine_bin, out_dir)
    os.mkdir(out_dir)
    local api_json = path.join(out_dir, "api.json")

    if not os.isfile(api_json) or os.mtime(engine_bin) > os.mtime(api_json) then
        cprint("${cyan}[codegen]${reset} %s --dump-api", engine_bin)
        os.vrunv(engine_bin, { "--dump-api", api_json }, { curdir = feather_root })
    end

    cprint("${cyan}[codegen]${reset} generate_bindings.py")
    os.vrunv("python3", {
        path.join(feather_root, "tools", "codegen", "generate_bindings.py"),
        "--api", api_json,
        "--out", out_dir,
    }, { curdir = feather_root })
end
