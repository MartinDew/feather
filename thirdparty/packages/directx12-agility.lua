-- Local override of xmake-repo's directx12-agility.
--
-- Identical to the upstream package except that on_install merges
-- feather_winsdk.cmake_configs() (-DCMAKE_LIBRARY_PATH=<WinSDK libs>) so the
-- port's find_library(d3d12 ...) resolves when cross-compiling with the
-- msvc-wine toolchain under a Clang compiler id. See
-- xmake/modules/feather_winsdk.lua and CROSS_COMPILE_FINDINGS.md (root cause #2).
--
-- The 4 port files (CMakeLists + 3 *.in) are vendored under
-- packages/port/directx12-agility/ because package:scriptdir() points at this
-- (packages/) directory, not xmake-repo's.
package("directx12-agility")
    set_homepage("https://devblogs.microsoft.com/directx/directx-12-agility-sdk")
    set_description("DirectX 12 Agility SDK")
    set_license("Microsoft")

    set_urls("https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/$(version)/#Microsoft.DXSDK.D3DX-$(version).zip")
    add_versions("1.619.2", "eb92d90bb23b2ec23410c41d791e41dbdbec942ab946924d1fdcb31eac6f0735")
    add_versions("1.618.1", "35a1bb4139f751e19956a0471fb621442450c63b1f100752b27ada6abed7a3da")

    add_configs("shared", {description = "Build shared library.", default = true, type = "boolean", readonly = true})

    add_deps("cmake")

    add_syslinks("d3d12")

    on_install("windows", function (package)
        os.cp("build/native/include", package:installdir())
        local REDIST_ARCH = ""
        if package:is_arch("arm64") then
            REDIST_ARCH = "arm64"
        elseif not package:is_arch("arm.*") and package:is_arch64() then
            REDIST_ARCH = "x64"
        elseif not package:is_arch("arm.*") and not package:is_arch64() then
            REDIST_ARCH = "win32"
        end
        os.cp(path.join("build/native/bin", REDIST_ARCH, "D3D12Core.dll"), package:installdir("bin"))
        os.cp(path.join("build/native/bin", REDIST_ARCH, "D3D12Core.pdb"), package:installdir("bin"))
        os.cp(path.join("build/native/bin", REDIST_ARCH, "d3d12SDKLayers.dll"), package:installdir("bin"))
        os.cp(path.join("build/native/bin", REDIST_ARCH, "d3d12SDKLayers.pdb"), package:installdir("bin"))
        os.cp(path.join("build/native/bin", REDIST_ARCH, "d3dconfig.exe"), package:installdir("bin"))
        os.cp(path.join("build/native/bin", REDIST_ARCH, "d3dconfig.pdb"), package:installdir("bin"))

        local port = path.join(package:scriptdir(), "port", "directx12-agility")
        os.cp(path.join(port, "directx12-agility-targets.cmake.in"), "directx12-agility-targets.cmake.in")
        os.cp(path.join(port, "directx12-agility-config.cmake.in"), "directx12-agility-config.cmake.in")
        os.cp(path.join(port, "directx12-agility.pc.in"), "directx12-agility.pc.in")
        os.cp(path.join(port, "cmakelists.txt"), "CMakeLists.txt")

        local configs = {
            "-DCMAKE_BUILD_TYPE=" .. (package:is_debug() and "Debug" or "Release"),
            "-DVERSION=" .. package:version()
        }
        -- WinSDK find_library paths when cross-compiling via msvc-wine (no-op otherwise)
        for _, c in ipairs(import("feather_winsdk").cmake_configs()) do table.insert(configs, c) end
        import("package.tools.cmake").install(package, configs)
    end)

    on_test(function (package)
        assert(package:check_cxxsnippets({test = [[
            void test() {
	            IDXGIAdapter1* dxgiAdapter = nullptr;
                ID3D12Device* device = nullptr;
                D3D12CreateDevice(dxgiAdapter, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
            }
        ]]}, {configs = {languages = "c++14"}, includes = {"dxgi.h", "d3d12.h"}}))
    end)
package_end()
