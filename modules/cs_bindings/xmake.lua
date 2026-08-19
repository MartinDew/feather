option("enable_cs_bindings")
    set_default(false)
    set_description("Generate C# bindings for Feather's public API via MRBind (not yet implemented; builds on c_bindings' output)")
option_end()

if not has_config("enable_cs_bindings") then
    return
end

-- Scaffolding only: mrbind_gen_csharp consumes mrbind_gen_c's
-- --output-desc-json (see modules/c_bindings/xmake.lua), so a real C#
-- pipeline is a follow-on to the C step, not parallel work. A phony target
-- (rather than no target at all) means enabling this option is visibly
-- "not yet implemented" instead of silently doing nothing.
target("cs_bindings")
    set_kind("phony")
    set_group("bindings")
    on_load(function (target)
        cprint("${yellow}[cs_bindings]${reset} not yet implemented -- enable_cs_bindings is scaffolding only;"
            .. " see modules/c_bindings for the working C pipeline this would build on"
            .. " (mrbind_gen_csharp consumes mrbind_gen_c's --output-desc-json)")
    end)
target_end()
