-- os.scriptdir(), not os.projectdir(): the latter would resolve to a
-- CONSUMER's repo if this file were ever includes()'d cross-repo.
local FEATHER_ROOT = path.directory(os.scriptdir())

-- Copies raw_resources/shaders next to the built executable. Modules needing
-- their own post-build deploy steps should define their own rule (see
-- vex_renderer.deploy_runtime) -- rules stack across a target, closures don't.
rule("feather.deploy_shaders")
    after_build(function(target)
        os.cp(
            path.join(FEATHER_ROOT, "raw_resources", "shaders"),
            path.join(target:targetdir(), "shaders"))
    end)
rule_end()

-- Copies flecs/sdl3's shared .dll next to feather.exe on windows/mingw --
-- add_packages() only wires the import lib, not the runtime file itself.
rule("feather.deploy_shared_deps")
    after_build(function(target)
        if not target:is_plat("windows", "mingw") then return end
        for _, pkgname in ipairs({"flecs", "sdl3"}) do
            local pkg = target:pkg(pkgname)
            if pkg then
                -- installdir(subpath) ignores the arg, returns the root.
                local bindir = path.join(pkg:installdir(), "bin")
                if os.isdir(bindir) then
                    for _, f in ipairs(os.files(path.join(bindir, "*.dll"))) do
                        os.cp(f, target:targetdir())
                    end
                end
            end
        end
    end)
rule_end()

-- feather_module_target(name, module_dir, files, opts)
--
-- Creates a {name} static lib and re-opens the feather target to link it in.
-- Module-specific build/deploy logic belongs in a rule (opts.exe_rules).
--
-- opts:
--   exe_packages         : packages added to the executable
--   exe_packages_windows : same, Windows-only
--   exe_rules             : rule names attached to the executable
--   generated_files       : like `files`, but for codegen output that
--                           doesn't exist on disk until the first before_build.

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
        -- Not {public=true}: a consumer must never see this define.
        add_defines("FEATHER_BUILDING_ENGINE")
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
