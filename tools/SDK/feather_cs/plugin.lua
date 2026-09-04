-- Declares a C# extension, published with NativeAOT into an ordinary native
-- shared library the engine loads like a C one (no .NET runtime hosted).
--
--   opts.csproj          the project file, required
--   opts.api_json        the designated API file, required
--   opts.published_name  file dotnet publish emits (default: the csproj's own
--                          filename plus the host's native shared-library
--                          extension -- .dll/.so/.dylib; override if
--                          <AssemblyName> in the .csproj differs)
--   opts.output_name     name to stage into bin/ as (default: lib<name>.so,
--                          matching feather_c_plugin's own naming -- <name>.dll
--                          with no "lib" prefix on Windows, lib<name>.dylib on
--                          macOS)
--   opts.runtime         .NET RID passed to dotnet publish (default: the host's
--                          own RID -- NativeAOT cannot cross the OS boundary,
--                          so this is the only default that always works)

function feather_cs_plugin(name, opts)
    opts = opts or {}

    target(name)
        -- Phony: dotnet does the building. xmake only sequences it and stages
        -- the result.
        set_kind("phony")
        add_packages("mrbind_generators")

        on_build(function (target)
            import("feather_plugin_bindings")
            local out = feather_plugin_bindings.generate(target, opts, {csharp = true})
            feather_plugin_bindings.publish_csharp(target, opts, out)
        end)
    target_end()
end
