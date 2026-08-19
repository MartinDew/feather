package("vex")
    set_kind("library")
    set_homepage("https://github.com/Narvin-Chana/Vex")
    set_urls("https://github.com/Narvin-Chana/Vex.git", {branch = "main"})
    -- Must match VexDX12.cmake's DX_AGILITY_VERSION
    local AGILITY_VERSION = "618"
    if is_plat("windows") then
        add_deps("directx12-agility 1.618.1")
    end
    if is_plat("mingw") then
        add_deps("vulkan-headers 1.4.335+0", "vulkan-loader 1.4.335+0")
    end
    add_deps("cmake ~4.2.2")
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

        local vexdx12 = path.join("cmake", "VexDX12.cmake")
        if os.isfile(vexdx12) then
            local content = io.readfile(vexdx12)
            local patched, n = content:gsub(
                '%${PIX_EVENTS_DIR%}/include', "${PIX_EVENTS_DIR}/Include")
            if n > 0 then
                cprint("${cyan}[vex]${reset} Patched VexDX12.cmake: PIX_INCLUDE_DIR case (include -> Include)")
                io.writefile(vexdx12, patched)
            end
        end

        local dxc_compiler_h = path.join("src", "ShaderCompiler", "Compiler", "DXC", "DXCCompiler.h")
        if os.isfile(dxc_compiler_h) then
            local content = io.readfile(dxc_compiler_h)
            local patched, n = content:gsub("<Unknwn%.h>", "<unknwn.h>")
            if n > 0 then
                cprint("${cyan}[vex]${reset} Patched DXCCompiler.h: Unknwn.h case (Unknwn.h -> unknwn.h)")
                io.writefile(dxc_compiler_h, patched)
            end
        end

        local function ensure_include(relpath, header)
            if not os.isfile(relpath) then
                return
            end
            local content = io.readfile(relpath)
            if content:find("#include <" .. header .. ">", 1, true) then
                return
            end
            local patched, n = content:gsub('(#include%s+"[^"]+"\n)', "%1\n#include <" .. header .. ">\n", 1)
            if n > 0 then
                cprint("${cyan}[vex]${reset} Patched %s: added missing #include <%s>", path.filename(relpath), header)
                io.writefile(relpath, patched)
            end
        end
        ensure_include(path.join("src", "Vex", "Logger.cpp"), "chrono")
        ensure_include(path.join("src", "RHI", "RHIResourceLayout.cpp"), "cstring")
        ensure_include(path.join("src", "ShaderCompiler", "Compiler", "DXC", "DXCCompiler.cpp"), "cstring")
        ensure_include(path.join("src", "Vex", "PipelineStateCache.cpp"), "algorithm")

        local configs = {
            "-DVEX_ENABLE_SLANG=ON",
            "-DVEX_BUILD_EXAMPLES=OFF",
            "-DVEX_BUILD_TESTS=OFF",
            "-DVEX_BUILD_TOOLS=OFF",
            "-DCMAKE_WINDOWS_EXPORT_ALL_SYMBOLS=ON",
            "-DCMAKE_CXX_STANDARD=23",
        }
        if package:is_plat("windows") then
            -- table.insert(configs, "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW") -- so MSVC_RUNTIME_LIBRARY below takes effect
        end
        if package:is_plat("linux") then
            -- Match whatever compiler xmake resolved instead of cmake's autodetected /usr/bin/c++
            local cc  = package:tool("cc")
            local cxx = package:tool("cxx")
            if cc  then table.insert(configs, "-DCMAKE_C_COMPILER="   .. cc)  end
            if cxx then table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. cxx) end
        end
        if package:is_plat("mingw") then
            table.insert(configs, "-DVEX_GRAPHICS_BACKEND=VULKAN")

            local vk_headers = package:dep("vulkan-headers"):fetch()
            local vk_loader  = package:dep("vulkan-loader"):fetch()
            if vk_headers and vk_headers.includedirs and vk_headers.includedirs[1] then
                table.insert(configs, "-DVulkan_INCLUDE_DIR=" .. vk_headers.includedirs[1])
            end
            if vk_loader and vk_loader.linkdirs and vk_loader.linkdirs[1] then
                table.insert(configs, "-DVulkan_LIBRARY=" .. path.join(vk_loader.linkdirs[1], "libvulkan-1.dll.a"))
            end

            local uuid_shim = path.join(builddir, "mingw_dxc_uuidof_shim.h")
            os.mkdir(builddir)
            io.writefile(uuid_shim, [[
#pragma once
#include <unknwn.h>
struct IDxcBlob;
struct IDxcBlobUtf8;
struct IDxcUtils;
struct IDxcResult;
struct IDxcCompiler3;
__CRT_UUID_DECL(IDxcBlob, 0x8BA5FB08,0x5195,0x40e2,0xAC,0x58,0x0D,0x98,0x9C,0x3A,0x01,0x02)
__CRT_UUID_DECL(IDxcBlobUtf8, 0x3DA636C9,0xBA71,0x4024,0xA3,0x01,0x30,0xCB,0xF1,0x25,0x30,0x5B)
__CRT_UUID_DECL(IDxcUtils, 0x4605C4CB,0x2019,0x492A,0xAD,0xA4,0x65,0xF2,0x0B,0xB7,0xD6,0x7F)
__CRT_UUID_DECL(IDxcResult, 0x58346CDA,0xDDE7,0x4497,0x94,0x61,0x6F,0x87,0xAF,0x5E,0x06,0x59)
__CRT_UUID_DECL(IDxcCompiler3, 0x228B4687,0x5A6A,0x4730,0x90,0x0C,0x97,0x02,0xB2,0x20,0x3F,0x54)
]])
            table.insert(configs, "-DCMAKE_CXX_FLAGS=-include " .. uuid_shim)
        end
        import("package.tools.cmake").install(package, configs, {
            builddir        = builddir,
            cmake_generator = "Ninja",
        })

        local deps = path.join(builddir, "_deps")

        local function pick_latest_dir(pattern)
            local dirs = os.dirs(path.join(deps, pattern))
            if #dirs == 0 then
                return nil
            end
            if #dirs > 1 then
                table.sort(dirs, function(a, b) return os.mtime(a) > os.mtime(b) end)
                cprint("${yellow}[vex]${reset} multiple dirs matched '%s' (%s), using most recently built: %s",
                    pattern, table.concat(dirs, ", "), dirs[1])
            end
            return dirs[1]
        end

        local slang_src = pick_latest_dir("slang*-src")
        if slang_src and os.isdir(slang_src) then
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

        -- Same FetchContent-name-includes-the-version pattern as slang above
        -- (cmake/VexDXC.cmake's DXC_NAME is "dxc_${DXC_VERSION}"), so this is
        -- glob-matched for the same reason.
        local dxc_src = pick_latest_dir("dxc*-src")
        if dxc_src and os.isdir(dxc_src) then
            local arch_map = {x86_64 = "x64", i386 = "x86", aarch64 = "arm64"}
            local arch = arch_map[package:arch()] or package:arch()
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
        elseif package:is_plat("mingw") then
        else
            local libdir = package:installdir("lib")
            local runtimedir = package:installdir("runtime")
            os.mkdir(runtimedir)
            if slang_src then
                for _, so in ipairs(os.files(path.join(slang_src, "lib", "libslang*.so*"))) do
                    os.cp(so, libdir)
                    os.cp(so, runtimedir)
                end
            end
            if dxc_src then
                for _, so in ipairs(os.files(path.join(dxc_src, "lib", "libdxcompiler.so*"))) do
                    os.cp(so, libdir)
                    os.cp(so, runtimedir)
                end
                -- libdxil.so is a runtime dep of dxcompiler, not linked directly
                for _, so in ipairs(os.files(path.join(dxc_src, "lib", "libdxil.so*"))) do
                    os.cp(so, runtimedir)
                end
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
        elseif package:is_plat("mingw") then
            if not os.isfile(path.join(libdir, "libVex.a")) then
                return nil
            end
            local vk_headers = package:dep("vulkan-headers"):fetch()
            local vk_loader  = package:dep("vulkan-loader"):fetch()
            return {
                includedirs = table.join(
                    {inc, path.join(inc, "magic_enum"), path.join(inc, "dxc"), path.join(inc, "slang")},
                    (vk_headers and vk_headers.includedirs) or {}
                ),
                linkdirs = table.join({libdir}, (vk_loader and vk_loader.linkdirs) or {}),
                links    = table.join(
                    {"Vex", "slang", "dxcompiler"},
                    (vk_loader and vk_loader.links) or {}
                ),
                defines  = {
                    "VEX_DX12=0",
                    "VEX_VULKAN=1",
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
