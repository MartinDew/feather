option("enable_vex_renderer")
    set_default(not is_plat("macosx"))
    set_description("Enable the vex_renderer module (off on Apple; Vex does not support Metal)")
option_end()

if has_config("enable_vex_renderer") then
    -- Owns everything the executables need for Vex's runtime deploy step, so
    -- that no target("feather.editor"/"feather.standalone") block below ever
    -- needs to redefine on_load/after_build — rules stack across a target,
    -- unlike those closures, so this composes cleanly with feather.deploy_shaders
    -- (xmake/helper.lua) and any other module's own deploy rule.
    rule("vex_renderer.deploy_runtime")
        on_load(function(target)
            -- D3D12 reads D3D12SDKVersion/D3D12SDKPath from the main exe at
            -- startup, so DX12AgilitySDK.cpp must compile into the exe itself.
            local vex = target:pkg("vex")
            if not vex then return end
            local agility_src = path.join(vex:installdir(), "src", "DX12", "DX12AgilitySDK.cpp")
            if os.isfile(agility_src) then
                target:add("files", agility_src)
                -- DX12AgilitySDK.cpp does #include "DX12Headers.h" unqualified;
                -- Vex's on_fetch only exposes include/ (not include/DX12) since
                -- consumers normally write #include "DX12/DX12Headers.h".
                target:add("includedirs", path.join(vex:installdir(), "include", "DX12"))
                target:add("defines", "DIRECTX_AGILITY_SDK_VERSION=618")
                target:add("defines", "D3D12_AGILITY_SDK_ENABLED")
            end
        end)

        after_build(function(target)
            local vex = target:pkg("vex")
            if not vex then return end
            local tdir = target:targetdir()
            -- NOTE: target:pkg("vex"):installdir(...) ignores subpath args and
            -- always returns the package root (unlike package:installdir(...)
            -- inside on_install, which does honor them) — join subpaths manually.
            local root = vex:installdir()

            -- Runtime libs (Slang, DXC, WinPIX) → next to exe
            local runtime_dir = path.join(root, "runtime")
            if os.isdir(runtime_dir) then
                for _, pat in ipairs({"*.dll", "*.so*"}) do
                    for _, f in ipairs(os.files(path.join(runtime_dir, pat))) do
                        os.cp(f, tdir)
                    end
                end
            end

            -- D3D12 Agility SDK DLLs → <targetdir>/D3D12/
            -- Sourced from the xrepo directx12-agility package rather than Vex's
            -- own internal FetchContent _deps folder (see thirdparty/xmake.lua).
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

            -- Vex HLSL/Slang shaders → next to exe
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
        -- Pull vex package onto the executables so target:pkg("vex") resolves
        -- in vex_renderer.deploy_runtime's hooks. Link deduplication in xmake
        -- prevents double-linking with vex_renderer.
        exe_packages = {"vex"},
        exe_packages_windows = {"directx12-agility"},
        exe_rules = {"vex_renderer.deploy_runtime"},
    })

    -- Vex-specific settings for both variants
    for _, variant in ipairs({"editor", "standalone"}) do
        target("vex_renderer_" .. variant)
            add_packages("vex", {public = false})
            -- Mirror CMake's per-config Vex defines
            if is_mode("debug") then
                add_defines("VEX_DEBUG=1", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=0")
            elseif is_mode("releasedbg") then
                add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=1", "VEX_SHIPPING=0")
            elseif is_mode("release") then
                add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=1")
            end
        target_end()
    end
end
