-- Automatic cross-compilation toolchain selection.
--
-- When the configured target platform/arch differs from the host, pick an
-- appropriate cross toolchain automatically so a plain
--   xmake f -p <plat> -a <arch>
-- Just Works instead of requiring the caller to also spell out --toolchain.
--
-- Dispatch (host is Linux/x86_64 in practice):
--   -> windows : msvc-wine toolchain (clang-cl + lld-link, MSVC ABI). Requires
--                --sdk=<msvc-wine sdk>; the toolchain's on_load asserts it.
--                mingw/llvm-mingw are deliberately NOT used (GNU ABI).
--   -> linux   : muslcc, xmake's built-in musl cross toolchain, auto-downloaded
--                via add_requires("muslcc").
--   -> other   : (e.g. macOS from Linux) not supported without an out-of-tree
--                SDK -- print guidance and fall back to the host toolchain
--                rather than misconfigure silently.
--
-- An explicit --toolchain=... always wins.
--
-- IMPORTANT: feather_setup_cross() MUST be called from the ROOT xmake.lua at
-- root/description scope (not from another includes()'d file, and not inside a
-- target block). set_toolchains()/add_requires() only bind the toolchain to
-- xrepo PACKAGE builds when issued at root scope -- issuing them from within an
-- includes()'d file's own top level leaves packages on the host compiler.
-- Defining the function here and calling it from root is the tested-good shape.

-- Register the msvc-wine toolchain definition (a global declaration; nested
-- includes register it fine) so --toolchain=msvc-wine and set_toolchains work.
includes(path.join(os.scriptdir(), "toolchains", "msvc-wine.lua"))

function feather_setup_cross()
    local host_plat, host_arch = os.host(), os.arch()
    local tgt_plat = get_config("plat") or host_plat
    local tgt_arch = get_config("arch") or host_arch

    if (tgt_plat == host_plat and tgt_arch == host_arch) or get_config("toolchain") then
        return
    end

    -- Only `print`/string.format are available at description scope (cprint,
    -- assert, raise are all nil here), so notices use plain print().
    if tgt_plat == "windows" then
        -- Surface the --sdk requirement early; the toolchain's on_load also
        -- asserts it, but that only fires once the toolchain is actually used.
        if not get_config("sdk") then
            print(string.format(
                "[feather/cross] targeting windows (%s): pass --sdk=<msvc-wine sdk>. "
                .. "e.g. xmake f -p windows -a %s --sdk=/path/to/msvc-wine/sdk",
                tgt_arch, tgt_arch))
        end
        set_toolchains("msvc-wine")
    elseif tgt_plat == "linux" then
        -- muslcc auto-downloads the matching <arch>-linux-musl cross toolchain.
        add_requires("muslcc")
        set_toolchains("@muslcc")
    else
        print(string.format(
            "[feather/cross] %s/%s (host) -> %s/%s (target): no auto cross toolchain "
            .. "for this target; pass --toolchain=<name> explicitly. Falling back to the "
            .. "host toolchain.",
            host_plat, host_arch, tgt_plat, tgt_arch))
    end
end
