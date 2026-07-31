-- feather.deploy_shaders
--
-- Copies raw_resources/shaders next to the built executable. Applied to
-- feather.editor/standalone at their initial declaration in root xmake.lua.
-- Modules needing their own post-build deploy steps should define their own
-- rule and list it via feather_module_target()'s exe_rules option instead of
-- an after_build closure — rules stack across a target, closures don't (see
-- vex_renderer.deploy_runtime for an example).
rule("feather.deploy_shaders")
    after_build(function(target)
        os.cp(
            path.join(os.projectdir(), "raw_resources", "shaders"),
            path.join(target:targetdir(), "shaders"))
    end)
rule_end()

-- feather_module_target(name, module_dir, files, opts)
--
-- Creates {name}_editor and {name}_standalone static libs and re-opens
-- feather.editor/standalone to link them in. Re-opening a target after its
-- initial declaration is valid in xmake, but only for additive declarative
-- calls (add_deps/add_packages/add_rules) — on_load/after_build/etc. are a
-- single script slot per target, so a second definition replaces rather than
-- stacks with the first. Module-specific build/deploy logic belongs in a
-- rule (opts.exe_rules) instead.
--
-- Prerequisites: feather.editor/standalone and feather_public_api must
-- already be declared, and the caller should have checked has_config("enable_<name>").
--
-- opts:
--   exe_packages         : packages added to the executables
--   exe_packages_windows : same, Windows-only
--   exe_rules             : rule names attached to the executables
--   generated_files       : like `files`, but for codegen output (e.g. a
--                           register_<name>_types.gen.cpp from
--                           generate_reflection.py --module-path) that doesn't
--                           exist on disk until the first before_build runs.
--                           Added with {always_added = true} so xmake doesn't
--                           fail description-time file-existence checks on it,
--                           matching GENERATED_SOURCE in the top-level xmake.lua.

function feather_module_target(name, module_dir, files, opts)
    opts = opts or {}

    for _, variant in ipairs({"editor", "standalone"}) do
        target(name .. "_" .. variant)
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
            add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
            if is_mode("debug", "releasedbg") then
                add_defines("BETA")
            end
            add_includedirs(os.projectdir(), {public = true})
            add_includedirs(path.join(os.projectdir(), "core"), {public = true})
            add_includedirs(module_dir, {public = false})
            add_deps("feather_public_api")
        target_end()
    end

    for _, variant in ipairs({"editor", "standalone"}) do
        target("feather." .. variant)
            add_deps(name .. "_" .. variant)
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
end
