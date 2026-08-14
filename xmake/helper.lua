-- Rooted at os.scriptdir() rather than $(projectdir)/os.projectdir(), same
-- reasoning as xmake/public_api.lua and xmake/engine.lua: those resolve to
-- whichever project is top-level for the current invocation, which would be
-- a CONSUMER's repo if this file were ever includes()'d cross-repo (it is
-- loaded by the root xmake.lua today, but feather_module_target() below is
-- also what a consumer's static build would need to fold modules in).
-- Captured once as a description-scope local and closed over by the
-- after_build callback below.
local FEATHER_ROOT = path.directory(os.scriptdir())

-- feather.deploy_shaders
--
-- Copies raw_resources/shaders next to the built executable. Applied to the
-- feather target at its initial declaration in xmake/engine.lua. Modules
-- needing their own post-build deploy steps should define their own rule and
-- list it via feather_module_target()'s exe_rules option instead of an
-- after_build closure — rules stack across a target, closures don't (see
-- vex_renderer.deploy_runtime for an example).
rule("feather.deploy_shaders")
    after_build(function(target)
        os.cp(
            path.join(FEATHER_ROOT, "raw_resources", "shaders"),
            path.join(target:targetdir(), "shaders"))
    end)
rule_end()

-- feather_module_target(name, module_dir, files, opts)
--
-- Creates a {name} static lib and re-opens the feather target to link it in.
-- Re-opening a target after its initial declaration is valid in xmake, but
-- only for additive declarative calls (add_deps/add_packages/add_rules) —
-- on_load/after_build/etc. are a single script slot per target, so a second
-- definition replaces rather than stacks with the first. Module-specific
-- build/deploy logic belongs in a rule (opts.exe_rules) instead.
--
-- Prerequisites: the feather target and feather_public_api must already be
-- declared, and the caller should have checked has_config("enable_<name>").
--
-- opts:
--   exe_packages         : packages added to the executable
--   exe_packages_windows : same, Windows-only
--   exe_rules             : rule names attached to the executable
--   generated_files       : like `files`, but for codegen output (e.g. a
--                           register_<name>_types.gen.cpp from
--                           generate_reflection.py --module-path) that doesn't
--                           exist on disk until the first before_build runs.
--                           Added with {always_added = true} so xmake doesn't
--                           fail description-time file-existence checks on it,
--                           matching GENERATED_SOURCE in xmake/engine.lua.

function feather_module_target(name, module_dir, files, opts)
    opts = opts or {}

    target(name)
        set_kind("static")
        set_warnings("none")
        set_group("modules")
        for _, f in ipairs(files or {}) do
            add_files(path.join(module_dir, f))
        end
        for _, f in ipairs(opts.generated_files or {}) do
            add_files(path.join(module_dir, f), {always_added = true})
        end
        add_defines(name .. "_ENABLED", {public = true})
        if is_mode("debug", "releasedbg") then
            add_defines("BETA")
        end
        add_includedirs(FEATHER_ROOT, {public = true})
        add_includedirs(path.join(FEATHER_ROOT, "core"), {public = true})
        add_includedirs(module_dir, {public = false})
        add_deps("feather_public_api")
    target_end()

    target("feather")
        add_deps(name)
        for _, pkg in ipairs(opts.exe_packages or {}) do
            add_packages(pkg)
        end
        if is_plat("windows") then
            for _, pkg in ipairs(opts.exe_packages_windows or {}) do
                add_packages(pkg)
            end
        end
        for _, rulename in ipairs(opts.exe_rules or {}) do
            add_rules(rulename)
        end
    target_end()
end
