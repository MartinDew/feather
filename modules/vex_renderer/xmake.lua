option("enable_vex_renderer")
    set_default(not is_plat("macosx"))
    set_description("Enable the vex_renderer module (off on Apple; Vex does not support Metal)")
option_end()

if (is_plat("macosx")) then
    return
end

if has_config("enable_vex_renderer") then
    -- Owns everything the executable needs for Vex's runtime deploy step, so
    -- the feather target below never redefines on_load/after_build directly
    -- (rules stack; those closures don't — see xmake/helper.lua).
    rule("vex_renderer.deploy_runtime")
        on_load(function(target)
            -- D3D12 reads D3D12SDKVersion/D3D12SDKPath from the main exe at
            -- startup, so DX12AgilitySDK.cpp must compile into the exe itself.
            if not is_plat("windows") then return end
            local vex = target:pkg("vex")
            if not vex then return end
            local agility_src = path.join(vex:installdir(), "src", "DX12", "DX12AgilitySDK.cpp")
            if os.isfile(agility_src) then
                target:add("files", agility_src)
                -- DX12AgilitySDK.cpp includes "DX12Headers.h" unqualified; Vex's
                -- on_fetch only exposes include/, not include/DX12.
                target:add("includedirs", path.join(vex:installdir(), "include", "DX12"))
                target:add("defines", "DIRECTX_AGILITY_SDK_VERSION=618")
                target:add("defines", "D3D12_AGILITY_SDK_ENABLED")
            end
        end)

        after_build(function(target)
            local vex = target:pkg("vex")
            if not vex then return end
            local tdir = target:targetdir()
            if (not is_plat("windows")) then
                tdir = path.join(tdir, "runtime")
                os.mkdir(tdir)
            end
            -- target:pkg("vex"):installdir(subpath) ignores subpath args and
            -- always returns the package root, so join subpaths manually.
            local root = vex:installdir()

            local runtime_dir = path.join(root, "runtime")
            if os.isdir(runtime_dir) then
                for _, pat in ipairs({"*.dll", "*.so*"}) do
                    for _, f in ipairs(os.files(path.join(runtime_dir, pat))) do
                        os.cp(f, tdir)
                    end
                end
            end

            if (is_plat("windows")) then
                -- D3D12 Agility SDK DLLs come from xrepo's directx12-agility package,
                -- not Vex's internal _deps/ (see thirdparty/xmake.lua).
                local agility = target:pkg("directx12-agility")
                if agility then
                    local agility_bin = path.join(agility:installdir(), "bin")
                    if os.isdir(agility_bin) then
                        local d3d12_dst = path.join(tdir, "D3D12")
                        os.mkdir(d3d12_dst)
                        os.cp(path.join(agility_bin, "D3D12Core.dll"), d3d12_dst)
                        os.cp(path.join(agility_bin, "d3d12SDKLayers.dll"), d3d12_dst)
                    end
                end
            end

            local shaders_src = path.join(root, "shaders")
            if os.isdir(shaders_src) then
                os.cp(path.join(shaders_src, "*"), tdir)
            end
        end)
    rule_end()

    feather_module_target("vex_renderer", os.scriptdir(), {
        "register_module.cpp",
        "vex_renderer.cpp",
    }, {
        -- Pull vex onto the executable so target:pkg("vex") resolves in
        -- vex_renderer.deploy_runtime's hooks (xmake dedupes the link).
        exe_packages = {"vex"},
        exe_packages_windows = {"directx12-agility"},
        exe_rules = {"vex_renderer.deploy_runtime"},
        -- Produced by generate_reflection.py --module-path (see run_codegen in
        -- xmake/engine.lua): the module's _bind_members() definitions,
        -- generated the same way core/*/register_<sub>_types.gen.cpp are.
        generated_files = {"register_vex_renderer_types.gen.cpp"},
    })

    target("vex_renderer")
        add_packages("vex", {public = false})
        -- vex_renderer is a dependency of feather (added via
        -- feather_module_target -> add_deps), so xmake compiles its files --
        -- including the generated register_vex_renderer_types.gen.cpp --
        -- before feather's own before_build(run_codegen) would get a chance
        -- to produce it. Generate it here instead; see
        -- xmake/modules/feather_codegen.lua's run_module_codegen.
        before_build(function(target)
            import("feather_codegen")
            feather_codegen.run_module_codegen(os.scriptdir())
        end)
        -- Mirrors CMake's per-config Vex defines
        if is_mode("debug") then
            add_defines("VEX_DEBUG=1", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=0")
        elseif is_mode("releasedbg") then
            add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=1", "VEX_SHIPPING=0")
        elseif is_mode("release") then
            add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=1")
        end
    target_end()
end
