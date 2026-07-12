-- feather.deploy_shaders
--
-- Copies raw_resources/shaders next to the built executable. Applied to
-- feather.editor/standalone at their initial declaration in the root
-- xmake.lua. Modules that need their own post-build deploy steps (copying
-- DLLs, vendor shaders, etc.) define their own rule and list it in the
-- `exe_rules` option of feather_module_target() below — rules stack, so
-- this one keeps running unchanged no matter how many modules attach
-- their own after_build behaviour.
rule("feather.deploy_shaders")
    after_build(function(target)
        os.cp(
            path.join(os.projectdir(), "raw_resources", "shaders"),
            path.join(target:targetdir(), "shaders"))
    end)
rule_end()

-- feather_module_target(name, module_dir, files, opts)
--
-- Creates {name}_editor and {name}_standalone static library targets, then
-- re-opens feather.editor / feather.standalone to wire them in. Re-opening
-- targets after their initial declaration is valid in xmake, but the *only*
-- things it should ever be used for are additive, declarative properties
-- (add_deps/add_packages/add_rules) — never for on_load/after_build/etc.
-- closures, since those are single-slot per target and a second definition
-- silently replaces the first rather than stacking. Any module-specific
-- build/deploy logic belongs in a rule (see vex_renderer.deploy_runtime for
-- an example) passed in via opts.exe_rules, since rules DO stack.
--
-- Prerequisites:
--   - feather.editor and feather.standalone must already be declared.
--   - feather_public_api must already be declared (provides engine includes
--     and public thirdparty headers without creating a circular dep).
--   - The caller's xmake.lua must have already checked has_config("enable_<name>").
--
-- name       : module name, e.g. "vex_renderer"
-- module_dir : os.scriptdir() from the calling xmake.lua
-- files      : list of .cpp filenames relative to module_dir
-- opts       : optional table of:
--   exe_packages         : packages to add to the executables (public API the
--                           module's own rules/hooks need via target:pkg())
--   exe_packages_windows : same, but only added on is_plat("windows")
--   exe_rules             : rule names to attach to the executables for
--                           module-specific post-build/on_load behaviour

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
            add_defines(name .. "_ENABLED", {public = true})
            add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
            if is_mode("debug", "releasedbg") then
                add_defines("BETA")
            end
            -- Public include dirs so consumers of this module see engine headers
            add_includedirs(os.projectdir(), {public = true})
            add_includedirs(path.join(os.projectdir(), "core"), {public = true})
            -- Private: module's own header dir
            add_includedirs(module_dir, {public = false})
            -- Pull in engine public API (includes + public thirdparty headers)
            -- without depending on the executables (which would be circular)
            add_deps("feather_public_api")
        target_end()
    end

    -- Wire module variants into the main executables. This reopening is
    -- purely declarative (deps/packages/rules), so it never needs to know
    -- about or repeat another module's build steps.
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
