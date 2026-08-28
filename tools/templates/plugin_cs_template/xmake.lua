-- Template for a Feather extension written in C#.
--
-- Copy this directory into your project, then:
--   1. rename the target, the csproj and the entry point (marked TODO below and
--      in my_plugin.fext / src/MyPlugin.cs)
--   2. copy the engine's published API description into api/:
--        cd <FeatherEngine> && xmake export-api
--        cp build/bindings/dist/feather_api.* <your project>/api/
--   3. copy the SDK itself in, once:
--        cp -r <FeatherEngine>/tools/SDK/{FeatherPluginSDK.lua,modules,packages} <your project>/sdk/
--   4. xmake     (requires the .NET SDK on PATH)
--
-- The result is published with NativeAOT, so it is an ordinary native shared
-- library: the engine loads it exactly like a C extension and hosts no .NET
-- runtime of its own.
set_xmakever("2.9.0")
set_project("my_plugin") -- TODO: rename
add_rules("mode.debug", "mode.releasedbg", "mode.release")

includes("sdk/FeatherPluginSDK.lua")
feather_plugin_sdk_init()

feather_cs_plugin("my_plugin", { -- TODO: rename
    csproj = "src/MyPlugin.csproj",
    api_json = "api/feather_api.json",
    -- dotnet names its output after the assembly; output_name is what gets
    -- staged into bin/, and must match the .fext "libraries" entry.
    published_name = "MyPlugin.so",
    output_name = "libmy_plugin.so",
})
