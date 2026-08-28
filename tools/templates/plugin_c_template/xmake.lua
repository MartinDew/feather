-- Template for a Feather extension written in C.
--
-- Copy this directory into your project, then:
--   1. rename the target and the entry point (marked TODO below, and in
--      my_plugin.fext and src/my_plugin.c)
--   2. copy the engine's published API description into api/:
--        cd <FeatherEngine> && xmake export-api
--        cp build/bindings/dist/feather_api.* <your project>/api/
--   3. copy the SDK itself in, once:
--        cp -r <FeatherEngine>/tools/SDK/{FeatherPluginSDK.lua,modules,packages} <your project>/sdk/
--   4. xmake
--
-- There is deliberately no path to a FeatherEngine checkout anywhere in here.
-- The api/ file is the entire interface: the SDK generates C headers from it,
-- and the engine supplies the implementation at runtime.
set_xmakever("2.9.0")
set_project("my_plugin") -- TODO: rename
set_languages("clatest")
add_rules("mode.debug", "mode.releasedbg", "mode.release")

includes("sdk/FeatherPluginSDK.lua")
feather_plugin_sdk_init()

feather_c_plugin("my_plugin", { -- TODO: rename (must match the .fext "libraries" entry)
    files = "src/*.c",
    api_json = "api/feather_api.json",
})
