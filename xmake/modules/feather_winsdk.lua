-- feather_winsdk: extra CMake configs so xrepo CMake thirdparties can locate the
-- Windows SDK / MSVC import libs when cross-compiling with the msvc-wine
-- toolchain.
--
-- Why this is needed: with a Clang-family compiler id, CMake's find_library does
-- NOT consult $ENV{LIB}, and xmake's package cmake builder overwrites any ambient
-- CMAKE_LIBRARY_PATH with a deps-only value (see
-- /usr/share/xmake/modules/package/tools/cmake.lua buildenvs()). So the only
-- reliable way to make find_library() see the WinSDK/MSVC lib dirs is to pass
-- them as an explicit -DCMAKE_LIBRARY_PATH on the cmake configure line.
--
-- Usage from a package's on_install():
--   import("feather_winsdk")
--   table.join2(configs, feather_winsdk.cmake_configs())   -- no-op off msvc-wine
--
-- Requires add_moduledirs("xmake/modules") in the root xmake.lua.

import("core.project.config")

-- Returns { "-DCMAKE_LIBRARY_PATH=<um>;<ucrt>;<msvc lib>" } when cross-compiling
-- to Windows via the msvc-wine SDK, otherwise {} (so it is safe to unconditionally
-- merge into every CMake package's configs).
function cmake_configs()
    if config.plat() ~= "windows" then
        return {}
    end
    local sdk = config.get("sdk")
    if not (sdk and os.isdir(sdk)) then
        -- Native Windows/MSVC build (or no SDK): CMake finds libs itself.
        return {}
    end

    local arch = config.arch() or "x64"
    local arch_dir = ({x64 = "x64", x86_64 = "x64", x86 = "x86", i386 = "x86", arm64 = "arm64"})[arch] or "x64"
    local triple_arch = ({x64 = "x86_64", x86_64 = "x86_64", x86 = "i386", i386 = "i386", arm64 = "aarch64"})[arch] or "x86_64"
    local target = triple_arch .. "-pc-windows-msvc"

    local function newest_subdir(dir)
        local subs = os.dirs(path.join(dir, "*"))
        table.sort(subs)
        return #subs > 0 and subs[#subs] or nil
    end

    local libdirs = {}
    local msvc_dir = newest_subdir(path.join(sdk, "VC", "Tools", "MSVC"))
    if msvc_dir then
        table.insert(libdirs, path.join(msvc_dir, "lib", arch_dir))
    end
    local winkit_lib = newest_subdir(path.join(sdk, "Windows Kits", "10", "Lib"))
    if winkit_lib then
        table.insert(libdirs, path.join(winkit_lib, "um", arch_dir))
        table.insert(libdirs, path.join(winkit_lib, "ucrt", arch_dir))
    end

    local existing = {}
    for _, dir in ipairs(libdirs) do
        if os.isdir(dir) then
            table.insert(existing, dir)
        end
    end
    if #existing == 0 then
        return {}
    end

    -- Package links run through the clang-cl *driver* (CMAKE_C_COMPILER=clang-cl),
    -- not lld-link directly, so their link line needs the target triple and an
    -- explicit lld linker (clang-cl otherwise looks for link.exe and fails with
    -- posix_spawn on a non-Windows host). Library search dirs come from the LIB
    -- env the toolchain exports; CMAKE_LIBRARY_PATH additionally feeds
    -- find_library(), which ignores $ENV{LIB} under a Clang compiler id.
    local link_flags = "--target=" .. target .. " -fuse-ld=lld"
    return {
        "-DCMAKE_LIBRARY_PATH=" .. table.concat(existing, ";"),
        "-DCMAKE_EXE_LINKER_FLAGS=" .. link_flags,
        "-DCMAKE_SHARED_LINKER_FLAGS=" .. link_flags,
        "-DCMAKE_MODULE_LINKER_FLAGS=" .. link_flags,
    }
end
