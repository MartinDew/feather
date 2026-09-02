-- Template for a Feather extension written in C#.
--
-- Copy this directory into your project, then:
--   1. rename the target and the csproj (marked TODO below); write your own
--      [FeatherComponent]/[FeatherSystem]/[FeatherInit] types in
--      src/MyPlugin.cs -- no entry point or manifest name to invent, the SDK's
--      bootstrap supplies those (see that file's own comment).
--   2. copy the engine's published API description into api/:
--        cd <FeatherEngine> && xmake export-api
--        cp build/bindings/dist/feather_api.* <your project>/api/
--   3. copy the SDK itself in, once:
--        cp -r <FeatherEngine>/tools/SDK/{FeatherPluginSDK.lua,modules,packages,csharp} <your project>/sdk/
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
    -- published_name/output_name are left at the SDK's defaults, which are
    -- already host-aware (MyPlugin.{dll,so,dylib} staged as my_plugin.dll on
    -- Windows or libmy_plugin.{so,dylib} elsewhere) and already match
    -- my_plugin.fext's "libraries" table above -- only override them if you
    -- rename the csproj's <AssemblyName> away from the project name.
})
