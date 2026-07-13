package("vex")
    set_kind("library")
    set_homepage("https://github.com/Narvin-Chana/Vex")
    set_urls("https://github.com/Narvin-Chana/Vex.git", {branch = "main"})
    -- Must match VexDX12.cmake's DX_AGILITY_VERSION
    local AGILITY_VERSION = "618"
    if is_plat("windows") then
        add_deps("directx12-agility 1.618.1")
    end
    on_install(function(package)
        local installdir = package:installdir()
        local builddir = path.join(installdir, ".cmake_build")

        -- Remove an invalid Vulkan validation layer Vex enables unconditionally
        local vkrhi = path.join("src", "Vulkan", "RHI", "VkRHI.cpp")
        if os.isfile(vkrhi) then
            local content = io.readfile(vkrhi)
            local patched, n = content:gsub(
                'layers%.push_back%("VK_LAYER_KHRONOS_synchronization2"%);[^\n]*\n?', "")
            if n > 0 then
                cprint("${cyan}[vex]${reset} Patched VkRHI.cpp: removed VK_LAYER_KHRONOS_synchronization2")
                io.writefile(vkrhi, patched)
            end
        end

        local configs = {
            "-DVEX_ENABLE_SLANG=ON",
            "-DVEX_BUILD_EXAMPLES=OFF",
            "-DVEX_BUILD_TESTS=OFF",
            "-DVEX_BUILD_TOOLS=OFF",
            "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON",
            "-DCMAKE_CXX_STANDARD=23",
        }
        if package:is_plat("windows") then
            local vs_runtime = package:has_runtime() or "MT"
            local runtime_map = {
                MT = "MultiThreaded$<$<CONFIG:Debug>:Debug>",
                MD = "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL",
            }
            table.insert(configs, "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW") -- so MSVC_RUNTIME_LIBRARY below takes effect
            table.insert(configs, "-DCMAKE_MSVC_RUNTIME_LIBRARY=" .. runtime_map[vs_runtime])
        end
        if package:is_plat("linux") then
            -- Match whatever compiler xmake resolved instead of cmake's autodetected /usr/bin/c++
            local cc  = package:tool("cc")
            local cxx = package:tool("cxx")
            if cc  then table.insert(configs, "-DCMAKE_C_COMPILER="   .. cc)  end
            if cxx then table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. cxx) end
        end
        import("package.tools.cmake").install(package, configs, {
            builddir        = builddir,
            cmake_generator = "Ninja",
        })

        -- Collect runtime artifacts out of cmake's _deps for on_fetch()/deploy steps
        local deps = path.join(builddir, "_deps")

        local slang_src = path.join(deps, "slang-src")
        if os.isdir(slang_src) then
            for _, dll in ipairs(os.files(path.join(slang_src, "**.dll"))) do
                os.cp(dll, package:installdir("runtime"))
            end
            local libdir = package:installdir("lib")
            for _, lib in ipairs(os.files(path.join(slang_src, "**", "slang.lib"))) do
                os.cp(lib, libdir)
                break
            end
            -- Prebuilt archives put headers in include/, source builds in src/
            for _, inc_dir in ipairs({
                path.join(slang_src, "include"),
                path.join(slang_src, "src"),
            }) do
                if os.isdir(inc_dir) then
                    local dst = package:installdir("include", "slang")
                    os.mkdir(dst)
                    for _, hdr in ipairs(os.files(path.join(inc_dir, "*.h"))) do
                        os.cp(hdr, dst)
                    end
                    for _, hdr in ipairs(os.files(path.join(inc_dir, "*.hpp"))) do
                        os.cp(hdr, dst)
                    end
                    break
                end
            end
        end

        local dxc_src = path.join(deps, "dxc-src")
        if os.isdir(dxc_src) then
            local arch = package:arch()
            local dxc_lib_dir = path.join(dxc_src, "lib", arch)
            local dxc_bin_dir = path.join(dxc_src, "bin", arch)
            if not os.isdir(dxc_lib_dir) then dxc_lib_dir = dxc_src end
            if not os.isdir(dxc_bin_dir) then dxc_bin_dir = dxc_src end

            for _, dll in ipairs(os.files(path.join(dxc_bin_dir, "*.dll"))) do
                os.cp(dll, package:installdir("runtime"))
            end
            local libdir = package:installdir("lib")
            local dxc_lib = path.join(dxc_lib_dir, "dxcompiler.lib")
            if os.isfile(dxc_lib) then
                os.cp(dxc_lib, libdir)
            end
        end

        if package:is_plat("windows") then
            local pix_bin = path.join(deps, "PixEvents", "bin", "x64")
            if os.isdir(pix_bin) then
                os.cp(path.join(pix_bin, "WinPixEventRuntime.dll"), package:installdir("runtime"))
            end
            -- D3D12 Agility SDK DLLs come from the xrepo directx12-agility package instead
            -- of Vex's internal _deps/, so we don't depend on that layout (see thirdparty/xmake.lua)
            local libdir = package:installdir("lib")
            local pix_lib = path.join(deps, "PixEvents", "bin", "x64", "WinPixEventRuntime.lib")
            if os.isfile(pix_lib) then os.cp(pix_lib, libdir) end
        else
            local libdir = package:installdir("lib")
            local runtimedir = package:installdir("runtime")
            os.mkdir(runtimedir)
            for _, so in ipairs(os.files(path.join(deps, "slang-src", "lib", "libslang*.so*"))) do
                os.cp(so, libdir)
                os.cp(so, runtimedir)
            end
            for _, so in ipairs(os.files(path.join(deps, "dxc-src", "lib", "libdxcompiler.so*"))) do
                os.cp(so, libdir)
                os.cp(so, runtimedir)
            end
            -- libdxil.so is a runtime dep of dxcompiler, not linked directly
            for _, so in ipairs(os.files(path.join(deps, "dxc-src", "lib", "libdxil.so*"))) do
                os.cp(so, runtimedir)
            end
        end

        if os.isdir("shaders") then
            os.cp(path.join("shaders", "*"), package:installdir("shaders"))
        end

        -- Must be compiled into the host executable, not just linked
        local agility_src = path.join("src", "DX12", "DX12AgilitySDK.cpp")
        if os.isfile(agility_src) then
            local dst = package:installdir("src", "DX12")
            os.mkdir(dst)
            os.cp(agility_src, dst)
        end
    end)

    on_fetch(function(package)
        local libdir = package:installdir("lib")
        local inc    = package:installdir("include")

        if package:is_plat("windows") then
            if not os.isfile(path.join(libdir, "Vex.lib")) then
                return nil
            end
            return {
                -- magic_enum headers are double-nested (include/magic_enum/magic_enum/*.hpp)
                includedirs = {
                    inc,
                    path.join(inc, "magic_enum"),
                    path.join(inc, "directx"),
                    path.join(inc, "dxc"),
                    path.join(inc, "slang"),
                },
                linkdirs = {libdir},
                links    = {"Vex", "slang", "dxcompiler", "WinPixEventRuntime"},
                syslinks = {"d3d12", "dxgi", "dxguid"},
                defines  = {
                    "VEX_AGILITY_SDK_VERSION=" .. AGILITY_VERSION,
                    "VEX_DX12=1",
                    "VEX_VULKAN=0",
                    "VEX_SLANG=1",
                    "VEX_SHADER_COMPILER=1",
                    "VEX_DXC=1",
                },
            }
        else
            if not os.isfile(path.join(libdir, "libVex.a")) then
                return nil
            end
            -- Only add an out-of-tree VULKAN_SDK include/lib dir; adding /usr/include
            -- explicitly would reorder it ahead of libstdc++'s own headers.
            local vulkan_includedirs = {}
            local vulkan_linkdirs = {}
            local vulkan_sdk = os.getenv("VULKAN_SDK")
            if vulkan_sdk and os.isdir(path.join(vulkan_sdk, "include")) then
                table.insert(vulkan_includedirs, path.join(vulkan_sdk, "include"))
            end
            if vulkan_sdk and os.isdir(path.join(vulkan_sdk, "lib")) then
                table.insert(vulkan_linkdirs, path.join(vulkan_sdk, "lib"))
            end
            return {
                includedirs = table.join(
                    {inc, path.join(inc, "magic_enum"), path.join(inc, "dxc"), path.join(inc, "slang")},
                    vulkan_includedirs
                ),
                linkdirs = table.join({libdir}, vulkan_linkdirs),
                links    = {"Vex", "slang", "dxcompiler"},
                syslinks = {"vulkan"},
                defines  = {
                    "VEX_DX12=0",
                    "VEX_VULKAN=1",
                    "VEX_SLANG=1",
                    "VEX_SHADER_COMPILER=1",
                    "VEX_DXC=1",
                },
            }
        end
    end)
package_end()
