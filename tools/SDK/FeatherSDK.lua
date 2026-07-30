-- FeatherSDK.lua: replaces tools/generate_export.cmake.
--
-- External consumers ("project DLL" repos loaded at runtime by
-- feather.editor/feather.standalone via _load_extension()) locate a
-- FeatherEngine checkout themselves (see tools/templates/consumer_xmake_template.lua
-- for the standard discovery block), then:
--
--   includes("/path/to/feather/tools/SDK/FeatherSDK.lua")
--   feather_sdk_setup("mygame_plugin", "standalone")   -- or "editor"
--
--   target("mygame_plugin")                            -- SEPARATE, reopened block --
--       set_kind("shared")                              -- feather_sdk_setup() opens
--       add_files("src/*.cpp")                           -- and closes its own target
--   target_end()                                         -- scope internally, so this
--                                                         -- must come after, not nested
--                                                         -- inside it (mirrors how
--                                                         -- modules/vex_renderer/xmake.lua
--                                                         -- reopens vex_renderer_editor
--                                                         -- after feather_module_target()).
--
-- Public API surface (include dirs + thirdparty packages) comes entirely
-- from feather_public_api (xmake/public_api.lua) via one-hop add_deps() --
-- this file never hand-lists packages/includedirs itself, so a new public
-- thirdparty dependency or an SDK-exposed module needs zero edits here.
local FEATHER_ROOT = path.directory(os.scriptdir())

-- Same include order as root xmake.lua, so package configs (e.g. static_deps)
-- hash-match the engine's own already-built xrepo cache entries instead of
-- xrepo building a second (e.g. shared-lib) variant just for the consumer.
includes(path.join(FEATHER_ROOT, "xmake", "options.lua"))
includes(path.join(FEATHER_ROOT, "thirdparty", "xmake.lua"))
includes(path.join(FEATHER_ROOT, "xmake", "public_api.lua"))

function feather_sdk_setup(target_name, variant)
    variant = variant or "standalone"
    if variant ~= "editor" and variant ~= "standalone" then
        -- assert()/error() are unavailable at description scope (only usable
        -- inside callbacks), so this can't hard-fail cleanly here -- warn and
        -- fall back to "standalone" instead of silently miscompiling.
        print("[feather] feather_sdk_setup: variant must be 'editor' or 'standalone', got: " .. tostring(variant) .. " -- defaulting to 'standalone'")
        variant = "standalone"
    end

    target(target_name)
        add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
        add_deps("feather_public_api")

        local bin_dir = path.join(FEATHER_ROOT, "build", "bin")
        if is_plat("windows") then
            -- Companion .lib produced by feather.<variant>'s Phase-1 /DEF:
            -- ldflags in root xmake.lua (see xmake.lua's is_plat("windows") block).
            add_linkdirs(bin_dir)
            add_links("feather." .. variant)
        else
            -- add_links() can't resolve a bare "feather.editor" ELF exe here:
            -- gcc/clang only treat a link name as an exact filename when it
            -- ends in .a/.so, otherwise it becomes "-lfeather.editor", which
            -- searches for libfeather.editor.so. Link by exact raw path instead.
            add_ldflags(path.join(bin_dir, "feather." .. variant), {force = true})
        end
    target_end()
end
