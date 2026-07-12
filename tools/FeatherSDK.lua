-- FeatherSDK.lua: replaces tools/generate_export.cmake.
-- External consumers: includes("/path/to/feather/tools/FeatherSDK.lua"), then
-- feather_sdk_setup("mygame_plugin", "standalone"|"editor") inside their target.

local FEATHER_ROOT = path.directory(os.scriptdir())

function feather_sdk_setup(target_name, variant)
    variant = variant or "standalone"
    assert(variant == "editor" or variant == "standalone",
        "feather_sdk_setup: variant must be 'editor' or 'standalone', got: " .. tostring(variant))

    target(target_name)
        add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
        add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
        add_includedirs(FEATHER_ROOT, {public = true})
        add_includedirs(path.join(FEATHER_ROOT, "thirdparty", "DirectXMath"), {public = true})
        add_includedirs(path.join(FEATHER_ROOT, "thirdparty", "SimpleMath"),  {public = true})

        if is_plat("windows") then
            local build_dir = path.join(FEATHER_ROOT, "build", "bin")
            add_linkdirs(build_dir)
            add_links("feather." .. variant)
        else
            add_linkdirs(path.join(FEATHER_ROOT, "build", "bin"))
            add_links("feather." .. variant)
        end
    target_end()
end
