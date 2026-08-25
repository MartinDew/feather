-- Template xmake.lua for a downstream "project DLL" repo (a shared library
-- loaded at runtime via _load_extension()). Copy to your project root, fill
-- in the two <project_name> placeholders and your own add_files() list.
-- Requires the FeatherEngine checkout to already be built.
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

-- Description scope is heavily sandboxed (io/import/assert/error are all
-- nil), so a bad root can't hard-fail here -- print guidance and skip
-- includes(), letting the later feather_sdk_setup() call fail naturally.
local function looks_like_feather(dir)
    return dir ~= nil and dir ~= "" and os.isfile(path.join(dir, "tools", "SDK", "FeatherSDK.lua"))
end

local function resolve_feather_root()
    -- Explicit --feather_sdk_path is taken as-is, unvalidated.
    if has_config("feather_sdk_path") then
        return get_config("feather_sdk_path")
    end
    local env = os.getenv("FEATHER_ROOT")
    if looks_like_feather(env) then return env end
    local sibling = path.join(os.projectdir(), "..", "FeatherEngine")
    if looks_like_feather(sibling) then return path.normalize(path.absolute(sibling)) end
    return nil
end

local FEATHER_ROOT = resolve_feather_root()
if FEATHER_ROOT then
    includes(path.join(FEATHER_ROOT, "tools", "SDK", "FeatherSDK.lua"))

    -- Must be built with the same FeatherEngine configuration as the engine
    -- binary this DLL loads into. `name` sets the generated
    -- register_<name>_types entry point, defaulting to the dir's basename.
    feather_sdk_setup("<project_name>", { -- TODO: rename
        codegen_dirs = { {dir = "src", name = "<project_name>"} }, -- TODO: rename
    })

    -- Reopened: feather_sdk_setup() opens/closes its own target scope.
    target("<project_name>") -- TODO: same name as above
        set_kind("shared")
        -- Flat, not bin/$(mode): the engine finds this DLL by recursively
        -- scanning the project dir, so a per-mode subdir would leave stale copies.
        set_targetdir(path.join(os.projectdir(), "bin"))
        add_files("src/*.cpp")
    target_end()
else
    print("[feather] Could not locate a FeatherEngine checkout. Do one of:")
    print("[feather]   xmake f --feather_sdk_path=/path/to/FeatherEngine")
    print("[feather]   export FEATHER_ROOT=/path/to/FeatherEngine")
    print("[feather]   check out FeatherEngine next to this project")
end
-- --- end discovery block ---------------------------------------------------
