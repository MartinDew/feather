-- Template xmake.lua for a downstream "project DLL" repo (a shared library
-- loaded at runtime by feather.editor/feather.standalone via
-- _load_extension() -- see core/resources/extension.h).
--
-- Copy this file to your project root as xmake.lua and fill in the two
-- <project_name> placeholders below plus your own add_files() list. This
-- discovery block is intentionally copy-paste: FeatherEngine can't ship it
-- as an includes()-able helper, since you need to already know a path to
-- the engine before you can includes() anything it owns. Everything past
-- the "end discovery block" marker -- thirdparty packages, public include
-- dirs, link setup -- is engine-owned and automatic: it never needs to
-- change here when the engine adds a new public dependency or module.
set_xmakever("2.9.0")
set_project("<project_name>") -- TODO: rename
set_languages("cxx23")
add_rules("mode.debug", "mode.releasedbg", "mode.release")

-- --- FeatherEngine discovery (copy-paste block; do not modify) -----------
-- Resolution order, mirroring the old CMake find_package(Feather) fallback:
--   1. xmake config option:  xmake f --feather_sdk_path=/path/to/FeatherEngine
--   2. feather_dir.txt file in this project's root (gitignored)
--   3. FEATHER_ROOT environment variable
option("feather_sdk_path")
    set_default(nil)
    set_showmenu(true)
    set_description("Absolute path to a FeatherEngine checkout")
option_end()

-- Note: error()/raise()/os.raise()/assert() are all unavailable at this
-- (description) scope -- only usable inside callbacks like on_load/before_build.
-- So an unresolved root can't hard-fail cleanly here; print guidance and skip
-- includes(), letting the later feather_sdk_setup() call fail naturally.
local function resolve_feather_root()
    if has_config("feather_sdk_path") then
        return get_config("feather_sdk_path")
    end
    local dirfile = path.join(os.projectdir(), "feather_dir.txt")
    if os.isfile(dirfile) then
        local p = io.readfile(dirfile):trim()
        if p and p ~= "" then return p end
    end
    local env = os.getenv("FEATHER_ROOT")
    if env and env ~= "" then return env end
    return nil
end

local FEATHER_ROOT = resolve_feather_root()
if FEATHER_ROOT then
    includes(path.join(FEATHER_ROOT, "tools", "FeatherSDK.lua"))

    -- "editor" or "standalone" -- must match which engine binary this DLL
    -- will be loaded into.
    feather_sdk_setup("<project_name>", "editor") -- TODO: rename + pick variant

    -- Separate, reopened block: feather_sdk_setup() opens and closes its own
    -- target scope internally, so this must come after it, not nested inside it.
    target("<project_name>") -- TODO: same name as above
        set_kind("shared")
        set_targetdir(path.join(os.projectdir(), "bin", "$(mode)"))
        add_files("src/*.cpp")
    target_end()
else
    -- Don't crash: an unresolved root must still let this script finish
    -- cleanly (e.g. so `xmake f --feather_sdk_path=...` can register the
    -- option above in the first place -- a hard failure here would abort
    -- before that registration completes).
    print("[feather] Could not locate a FeatherEngine checkout. Set one of:")
    print("[feather]   xmake f --feather_sdk_path=/path/to/FeatherEngine")
    print("[feather]   echo /path/to/FeatherEngine > feather_dir.txt   (gitignored)")
    print("[feather]   export FEATHER_ROOT=/path/to/FeatherEngine")
end
-- --- end discovery block ---------------------------------------------------
