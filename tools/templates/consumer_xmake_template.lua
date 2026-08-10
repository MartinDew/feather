-- Template xmake.lua for a downstream "project DLL" repo (a shared library
-- loaded at runtime by the feather executable via _load_extension() -- see
-- core/resources/extension.h).
--
-- Copy this file to your project root as xmake.lua and fill in the two
-- <project_name> placeholders below plus your own add_files() list. This
-- discovery block is intentionally copy-paste: FeatherEngine can't ship it
-- as an includes()-able helper, since you need to already know a path to
-- the engine before you can includes() anything it owns. Everything past
-- the "end discovery block" marker -- thirdparty packages, public include
-- dirs, link setup -- is engine-owned and automatic: it never needs to
-- change here when the engine adds a new public dependency or module.
--
-- Prerequisite: the FeatherEngine checkout must already be BUILT. You link
-- against its executable, and reflection codegen here runs with --skip-core
-- (a consumer never writes into the engine's tree), so core's generated
-- headers have to exist already. feather_sdk_setup() checks this and fails
-- with a readable message rather than a linker error.
set_xmakever("2.9.0")
set_project("<project_name>") -- TODO: rename
set_languages("cxx23")
add_rules("mode.debug", "mode.releasedbg", "mode.release")

-- --- FeatherEngine discovery (copy-paste block; do not modify) -----------
-- Resolution order:
--   1. xmake config option:  xmake f --feather_sdk_path=/path/to/FeatherEngine
--      (persisted in this project's xmake config -- set it once)
--   2. FEATHER_ROOT environment variable
--   3. a sibling FeatherEngine checkout next to this project
option("feather_sdk_path")
    set_default(nil)
    set_showmenu(true)
    set_description("Absolute path to a FeatherEngine checkout")
option_end()

-- Note on what's usable here: description scope is heavily sandboxed. io,
-- import, assert, error, raise and try are ALL nil (verified on xmake 3.0.9);
-- only part of os.* (isdir/isfile/exists/getenv/projectdir/...), path.*,
-- includes() and print() survive. Two consequences:
--   * there is no way to read a path out of a config file here, hence the
--     three env/option/convention sources above and no feather_dir.txt;
--   * an unresolved root can't hard-fail cleanly, so print guidance and skip
--     includes(), letting the later feather_sdk_setup() call fail naturally.
local function looks_like_feather(dir)
    return dir ~= nil and dir ~= "" and os.isfile(path.join(dir, "tools", "SDK", "FeatherSDK.lua"))
end

local function resolve_feather_root()
    -- An explicit --feather_sdk_path is taken as-is, unvalidated: if the user
    -- named a bad path they want to hear about that path, not fall through to
    -- some other checkout that happens to be lying around.
    if has_config("feather_sdk_path") then
        return get_config("feather_sdk_path")
    end
    local env = os.getenv("FEATHER_ROOT")
    if looks_like_feather(env) then return env end
    -- Usual workspace layout: <workspace>/FeatherEngine alongside
    -- <workspace>/<this project>, so a fresh clone builds with zero config.
    -- normalize(): this path is what every later engine-relative path and
    -- diagnostic is built from, and an un-collapsed ".." leaks into all of them.
    local sibling = path.join(os.projectdir(), "..", "FeatherEngine")
    if looks_like_feather(sibling) then return path.normalize(path.absolute(sibling)) end
    return nil
end

local FEATHER_ROOT = resolve_feather_root()
if FEATHER_ROOT then
    includes(path.join(FEATHER_ROOT, "tools", "SDK", "FeatherSDK.lua"))

    -- codegen_dirs runs the engine's reflection generator over your sources,
    -- so FCLASS/FSTRUCT work here exactly as they do in the engine; `name` is
    -- what the generated entry points are called (register_<name>_types() /
    -- register_<name>_components()), defaulting to the dir's basename --
    -- override it, or a "src" dir gives register_src_types.
    feather_sdk_setup("<project_name>", { -- TODO: rename
        codegen_dirs = { {dir = "src", name = "<project_name>"} }, -- TODO: rename
    })

    -- Separate, reopened block: feather_sdk_setup() opens and closes its own
    -- target scope internally, so this must come after it, not nested inside it.
    target("<project_name>") -- TODO: same name as above
        set_kind("shared")
        -- Flat, NOT bin/$(mode): the engine finds this DLL by recursively
        -- scanning the project dir (ResourceLoader::index_project), so a
        -- per-mode subdir would leave stale copies from other modes lying
        -- around for it to load a second time.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files("src/*.cpp")
    target_end()
else
    -- Don't crash: an unresolved root must still let this script finish
    -- cleanly (e.g. so `xmake f --feather_sdk_path=...` can register the
    -- option above in the first place -- a hard failure here would abort
    -- before that registration completes).
    print("[feather] Could not locate a FeatherEngine checkout. Do one of:")
    print("[feather]   xmake f --feather_sdk_path=/path/to/FeatherEngine")
    print("[feather]   export FEATHER_ROOT=/path/to/FeatherEngine")
    print("[feather]   check out FeatherEngine next to this project")
end
-- --- end discovery block ---------------------------------------------------
