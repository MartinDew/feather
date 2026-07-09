package("vex")
    set_kind("library")
    set_homepage("https://github.com/Narvin-Chana/Vex")
    set_urls("https://github.com/Narvin-Chana/Vex.git", {branch = "main"})
    -- D3D12 Agility SDK version embedded in Vex (VexDX12.cmake: DX_AGILITY_VERSION = "618")
    local AGILITY_VERSION = "618"

    on_install(function(package)
        -- CWD is the package source dir in on_install
        local installdir = package:installdir()
        -- cmake build dir lives inside installdir so it is cleaned up with the package
        local builddir = path.join(installdir, ".cmake_build")

        -- ---- Patch: remove invalid Vulkan validation layer ---------------
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

        -- ---- cmake configure + build + install ---------------------------

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
            -- Force CMake's modern runtime-selection policy so the variable below
            -- actually takes effect instead of being silently ignored.
            table.insert(configs, "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW")
            table.insert(configs, "-DCMAKE_MSVC_RUNTIME_LIBRARY=" .. runtime_map[vs_runtime])
        end
        if package:is_plat("linux") then
            -- cmake would otherwise auto-detect /usr/bin/c++, ignoring the project
            -- toolchain. Follow whatever compiler xmake resolved for this package
            -- so Vex builds with the same compiler family as the engine. Both use
            -- the system libstdc++, whose ABI is shared across gcc and clang, so no
            -- -stdlib override is needed (a modern libstdc++ has <print>/<format>).
            local cc  = package:tool("cc")
            local cxx = package:tool("cxx")
            if cc  then table.insert(configs, "-DCMAKE_C_COMPILER="   .. cc)  end
            if cxx then table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. cxx) end
        end
        import("package.tools.cmake").install(package, configs, {
            builddir        = builddir,
            cmake_generator = "Ninja",
        })

        -- ---- Collect runtime artifacts from cmake's _deps ----------------
        local deps = path.join(builddir, "_deps")

        -- Slang: search recursively under _deps/slang-src for DLLs and the import lib.
        -- Pre-built slang releases nest by platform+config (bin/windows-x64/release/),
        -- while source builds may put outputs at bin/ directly — recursive glob handles both.
        local slang_src = path.join(deps, "slang-src")
        if os.isdir(slang_src) then
            -- DLLs → runtime/
            for _, dll in ipairs(os.files(path.join(slang_src, "**.dll"))) do
                os.cp(dll, package:installdir("runtime"))
            end
            -- slang.lib → lib/
            local libdir = package:installdir("lib")
            for _, lib in ipairs(os.files(path.join(slang_src, "**", "slang.lib"))) do
                os.cp(lib, libdir)
                break
            end
            -- slang headers → include/slang/
            -- Slang headers live under include/ in the prebuilt archive; copy them so
            -- consumers can #include <slang.h> via the includedirs returned by on_fetch.
            for _, inc_dir in ipairs({
                path.join(slang_src, "include"),
                path.join(slang_src, "src"),  -- source builds expose headers here
            }) do
                if os.isdir(inc_dir) then
                    local dst = package:installdir("include", "slang")
                    os.mkdir(dst)
                    -- Only copy .h/.hpp files to avoid pulling in sources
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
            if not os.isdir(dxc_lib_dir) then
                -- Fallback for source builds that don't nest by arch
                dxc_lib_dir = dxc_src
            end
            if not os.isdir(dxc_bin_dir) then
                dxc_bin_dir = dxc_src
            end

            -- DLLs → runtime/
            for _, dll in ipairs(os.files(path.join(dxc_bin_dir, "*.dll"))) do
                os.cp(dll, package:installdir("runtime"))
            end
            -- dxcompiler.lib → lib/
            local libdir = package:installdir("lib")
            local dxc_lib = path.join(dxc_lib_dir, "dxcompiler.lib")
            if os.isfile(dxc_lib) then
                os.cp(dxc_lib, libdir)
            end
        end

        if package:is_plat("windows") then
            -- WinPIX runtime (x64) → runtime/
            local pix_bin = path.join(deps, "PixEvents", "bin", "x64")
            if os.isdir(pix_bin) then
                os.cp(path.join(pix_bin, "WinPixEventRuntime.dll"), package:installdir("runtime"))
            end

            -- D3D12 Agility SDK DLLs (x64) → D3D12/
            for _, agility_dir in ipairs(os.dirs(path.join(deps, "DirectX-AgilitySDK-*"))) do
                local x64_dir = path.join(agility_dir, "build", "native", "bin", "x64")
                if os.isdir(x64_dir) then
                    os.cp(path.join(x64_dir, "D3D12Core.dll"),       package:installdir("D3D12"))
                    os.cp(path.join(x64_dir, "d3d12SDKLayers.dll"),  package:installdir("D3D12"))
                end
            end

            -- PIX import lib
            local libdir = package:installdir("lib")
            local pix_lib = path.join(deps, "PixEvents", "bin", "x64", "WinPixEventRuntime.lib")
            if os.isfile(pix_lib) then os.cp(pix_lib, libdir) end
        else
            -- Linux: copy slang and dxc shared libs so the linker and runtime can find them
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
            -- libdxil.so is a runtime dep of dxcompiler; not a direct link target
            for _, so in ipairs(os.files(path.join(deps, "dxc-src", "lib", "libdxil.so*"))) do
                os.cp(so, runtimedir)
            end
        end

        -- Vex shaders → shaders/
        if os.isdir("shaders") then
            os.cp(path.join("shaders", "*"), package:installdir("shaders"))
        end

        -- DX12AgilitySDK.cpp → src/DX12/  (must be compiled into the host executable)
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
            -- Use Vex.lib as the installed indicator (always produced by cmake's install step)
            if not os.isfile(path.join(libdir, "Vex.lib")) then
                return nil
            end
            return {
                -- magic_enum is double-nested (include/magic_enum/magic_enum/magic_enum.hpp);
                -- adding include/magic_enum as a secondary root makes the #include work.
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
            -- Linux/Vulkan: cmake produces libVex.a; slang/dxc are shared libs
            if not os.isfile(path.join(libdir, "libVex.a")) then
                return nil
            end
            -- Vex headers include <vulkan/vulkan.hpp>; expose the Vulkan SDK include
            -- dir so consumers don't need to add it themselves.  VULKAN_SDK is set by
            -- humbletim/setup-vulkan-sdk on CI and by the LunarG SDK installer locally.
            -- Fall back to /usr/include for distros that ship vulkan-headers system-wide.
            -- Only add an explicit include dir when VULKAN_SDK points at an
            -- out-of-tree SDK.  Do NOT add /usr/include for distro-packaged
            -- vulkan-headers: it is already on the default search path, and
            -- passing it as -isystem /usr/include reorders it ahead of the
            -- libstdc++ headers, breaking their `#include_next <stdlib.h>`.
            local vulkan_includedirs = {}
            local vulkan_sdk = os.getenv("VULKAN_SDK")
            if vulkan_sdk and os.isdir(path.join(vulkan_sdk, "include")) then
                table.insert(vulkan_includedirs, path.join(vulkan_sdk, "include"))
            end
            return {
                includedirs = table.join(
                    {inc, path.join(inc, "magic_enum"), path.join(inc, "dxc"), path.join(inc, "slang")},
                    vulkan_includedirs
                ),
                linkdirs = {libdir},
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
