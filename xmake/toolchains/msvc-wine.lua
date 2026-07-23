-- msvc-wine cross toolchain (Linux/macOS host -> Windows, MSVC ABI)
--
-- Native clang-cl (compile) + lld-link (link) driving the headers/libs shipped
-- by an msvc-wine SDK (https://github.com/mstorsjo/msvc-wine). No wine is
-- invoked at compile time and no GNU/mingw ABI is produced -- the objects are
-- true x86_64-pc-windows-msvc. Auto-selected by xmake/cross.lua when the target
-- platform is windows and the host is not.
--
-- Usage (selected automatically by cross.lua, or manually):
--   xmake f -p windows -a x64 --toolchain=msvc-wine --sdk=<msvc-wine sdk>
-- The --sdk path is REQUIRED; on_load() raises a clear error without it.
--
-- HOW PATHS REACH THE TOOLS
-- Rather than passing include/lib dirs as flags (clang-cl and lld-link disagree
-- on flag dialect, and bare /libpath: fed to the clang-cl driver is treated as a
-- filename), we export INCLUDE and LIB into the environment -- exactly what
-- msvc-wine's msvcenv-native.sh does. clang-cl reads INCLUDE, lld-link reads LIB.
-- The engine's own targets link with lld-link directly (xmake cannot drive
-- clang-cl as a linker: it has no linkargv). CMake thirdparty packages instead
-- link through the clang-cl *driver*, which needs --target + -fuse-ld=lld and the
-- WinSDK lib dirs as -DCMAKE_LIBRARY_PATH; that is injected per package by
-- xmake/modules/feather_winsdk.lua (see feather_winsdk.cmake_configs()).
--
-- HOST PREREQUISITE (one-time, not per build): the SDK must be case-normalised
-- for a case-sensitive filesystem (ext4), otherwise mixed-case syslink names
-- from packages (e.g. flecs' "Dbghelp") miss. Prepare the SDK once with
-- msvc-wine's `lowercase -symlink` + `lowercase -map_winsdk`, or mount it via a
-- case-insensitive overlay (ciopfs). See CROSS_COMPILE_FINDINGS.md.

toolchain("msvc-wine")
    set_kind("cross")
    set_homepage("https://github.com/mstorsjo/msvc-wine")
    set_description("clang-cl + lld-link targeting Windows (MSVC ABI) via an msvc-wine SDK")

    -- MSVC-compatible driver: apply_compile_flags() in root xmake.lua treats
    -- clang_cl as MSVC and emits /W4 /Od /Zi etc., so the codebase's flag path
    -- is already correct for this toolchain.
    set_toolset("cc",   "clang-cl")
    set_toolset("cxx",  "clang-cl")
    set_toolset("ld",   "lld-link")
    set_toolset("sh",   "lld-link")
    set_toolset("ar",   "llvm-lib")
    set_toolset("mrc",  "llvm-rc")

    on_load(function (toolchain)
        import("core.project.config")

        local sdk = config.get("sdk")
        assert(sdk and os.isdir(sdk),
            "toolchain(msvc-wine): pass --sdk=<msvc-wine sdk> (an existing msvc-wine SDK directory)")

        -- Resolve target arch -> SDK lib subdir and clang triple arch.
        local arch = toolchain:arch() or "x64"
        local arch_dir = ({x64 = "x64", x86_64 = "x64", x86 = "x86", i386 = "x86", arm64 = "arm64"})[arch] or "x64"
        local triple_arch = ({x64 = "x86_64", x86_64 = "x86_64", x86 = "i386", i386 = "i386", arm64 = "aarch64"})[arch] or "x86_64"
        local target = triple_arch .. "-pc-windows-msvc"

        -- Discover the (single) versioned MSVC + Windows Kits dirs.
        local function newest_subdir(dir)
            local subs = os.dirs(path.join(dir, "*"))
            table.sort(subs)   -- version strings sort lexically; newest last
            return #subs > 0 and subs[#subs] or nil
        end

        local msvc_dir = newest_subdir(path.join(sdk, "VC", "Tools", "MSVC"))
        assert(msvc_dir, "toolchain(msvc-wine): no VC/Tools/MSVC/<ver> under " .. sdk)

        local winkit     = path.join(sdk, "Windows Kits", "10")
        local winkit_inc = newest_subdir(path.join(winkit, "Include"))
        local winkit_lib = newest_subdir(path.join(winkit, "Lib"))
        assert(winkit_inc and winkit_lib,
            "toolchain(msvc-wine): no Windows Kits/10/{Include,Lib}/<ver> under " .. sdk)

        local function existing(dirs)
            local out = {}
            for _, d in ipairs(dirs) do if os.isdir(d) then table.insert(out, d) end end
            return out
        end
        local includedirs = existing({
            path.join(msvc_dir, "include"),
            path.join(winkit_inc, "ucrt"),
            path.join(winkit_inc, "um"),
            path.join(winkit_inc, "shared"),
            path.join(winkit_inc, "winrt"),
            path.join(winkit_inc, "cppwinrt"),
        })
        local linkdirs = existing({
            path.join(msvc_dir, "lib", arch_dir),
            path.join(winkit_lib, "ucrt", arch_dir),
            path.join(winkit_lib, "um", arch_dir),
        })

        -- Export MSVC-style env for all child tools (clang-cl reads INCLUDE,
        -- lld-link reads LIB). ';' separator, matching msvcenv-native.sh, works
        -- on a non-Windows host too. os.setenv propagates to the cmake/ninja/
        -- clang-cl/lld-link subprocesses this xmake invocation spawns.
        os.setenv("INCLUDE", table.concat(includedirs, ";"))
        os.setenv("LIB",     table.concat(linkdirs, ";"))

        -- clang-cl on a non-Windows host must be told the target triple. Also
        -- pass -imsvc explicitly (belt-and-suspenders alongside INCLUDE) so the
        -- engine's own compiles never depend on env propagation timing.
        toolchain:add("cxflags", "--target=" .. target, {force = true})
        for _, dir in ipairs(includedirs) do
            toolchain:add("cxflags", "-imsvc" .. dir, {force = true})
        end

        -- -fuse-ld=lld goes on COMPILE flags on purpose. CMake reuses the compile
        -- flags on its link lines, so every clang-cl-driven link -- crucially
        -- including xrepo packages we don't override, and CMake's own
        -- compiler-detection link test -- finds lld-link instead of hunting for a
        -- nonexistent link.exe (posix_spawn failure). It is merely an unused-arg
        -- warning at compile time (silenced below), and it never reaches the
        -- engine's own link, which xmake drives through lld-link directly with
        -- ldflags only.
        toolchain:add("cxflags", "-fuse-ld=lld", "-Wno-unused-command-line-argument", {force = true})
        -- No ldflags here: the engine links with lld-link (which finds libs via
        -- LIB and needs neither --target nor -fuse-ld). Package exe/shared links
        -- go through clang-cl and get their flags from feather_winsdk instead.
    end)
toolchain_end()
