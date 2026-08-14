-- Template xmake.lua for a downstream plugin repo, loaded at runtime through
-- the C ABI (docs/plugin_abi.md). Copy to your project root, fill in the two
-- <project_name> placeholders and your own add_files() list. Requires the
-- FeatherEngine checkout to already be built.
set_xmakever("2.9.0")
set_project("<project_name>") -- TODO: rename
set_languages("cxx23")
add_rules("mode.debug", "mode.releasedbg", "mode.release")

-- --- FeatherEngine discovery (copy-paste block; do not modify) -----------
-- Resolution order: --feather_sdk_path option, FEATHER_ROOT env, sibling checkout.
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

    -- Sets target kind/targetdir and wires codegen (--dump-api then
    -- generate_bindings.py -> .gen/feather_bindings.gen.h); see FeatherSDK.lua.
    feather_sdk_setup("<project_name>") -- TODO: rename

    -- Reopened block: feather_sdk_setup() opens/closes its own target scope,
    -- so this must come after it, not nested inside it.
    target("<project_name>") -- TODO: same name as above
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
