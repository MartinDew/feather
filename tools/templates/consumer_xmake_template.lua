-- Template xmake.lua for a downstream "project DLL" repo (a shared library
-- loaded at runtime by feather.editor/feather.standalone via
-- _load_extension() -- see core/resources/extension.h).
--
-- Copy to your project root as xmake.lua, fill in the two <project_name>
-- placeholders and your own add_files() list. The discovery block is
-- intentionally copy-paste (can't be an includes()-able helper -- you need
-- the engine path before you can includes() anything it owns). Everything
-- past "end discovery block" is engine-owned and automatic.
--
-- Prerequisite: the FeatherEngine checkout must already be built -- you link
-- against its executable and codegen runs with --skip-core, so core's
-- generated headers must already exist. feather_sdk_setup() checks this and
-- fails with a readable message rather than a linker error.
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

-- Description scope is heavily sandboxed (io/import/assert/error/raise/try
-- are all nil), so a bad root can't hard-fail cleanly here -- print guidance
-- and skip includes(), letting the later feather_sdk_setup() call fail naturally.
local function looks_like_feather(dir)
    return dir ~= nil and dir ~= "" and os.isfile(path.join(dir, "tools", "SDK", "FeatherSDK.lua"))
end

local function resolve_feather_root()
    -- Explicit --feather_sdk_path is taken as-is, unvalidated: a bad path
    -- should surface as itself, not fall through to another checkout.
    if has_config("feather_sdk_path") then
        return get_config("feather_sdk_path")
    end
    local env = os.getenv("FEATHER_ROOT")
    if looks_like_feather(env) then return env end
    -- Usual layout: <workspace>/FeatherEngine next to this project.
    local sibling = path.join(os.projectdir(), "..", "FeatherEngine")
    if looks_like_feather(sibling) then return path.normalize(path.absolute(sibling)) end
    return nil
end

local FEATHER_ROOT = resolve_feather_root()
if FEATHER_ROOT then
    includes(path.join(FEATHER_ROOT, "tools", "SDK", "FeatherSDK.lua"))

    -- "editor" or "standalone" must match which engine binary this DLL loads
    -- into. `name` sets the generated register_<name>_types/_components
    -- entry point names, defaulting to the dir's basename.
    feather_sdk_setup("<project_name>", "editor", { -- TODO: rename + pick variant
        codegen_dirs = { {dir = "src", name = "<project_name>"} }, -- TODO: rename
    })

    -- Reopened block: feather_sdk_setup() opens/closes its own target scope,
    -- so this must come after it, not nested inside it.
    target("<project_name>") -- TODO: same name as above
        set_kind("shared")
        -- Flat, not bin/$(mode): the engine finds this DLL by recursively
        -- scanning the project dir (ResourceLoader::index_project), so a
        -- per-mode subdir would leave stale copies for it to load a second time.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files("src/*.cpp")
    target_end()
else
    -- Must not crash: `xmake f --feather_sdk_path=...` needs this script to
    -- finish so the option above gets registered.
    print("[feather] Could not locate a FeatherEngine checkout. Do one of:")
    print("[feather]   xmake f --feather_sdk_path=/path/to/FeatherEngine")
    print("[feather]   export FEATHER_ROOT=/path/to/FeatherEngine")
    print("[feather]   check out FeatherEngine next to this project")
end
-- --- end discovery block ---------------------------------------------------
