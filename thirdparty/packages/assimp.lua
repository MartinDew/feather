-- Custom assimp package that forces bundled unzip instead of system minizip.
--
-- Why: On Linux the system minizip pkgconfig advertises no include path
-- (headers live in /usr/include/minizip/ which pkgconfig omits as a
-- "standard" dir). assimp then finds minizip but gets empty
-- UNZIP_INCLUDE_DIRS, so #include <unzip.h> fails because the headers
-- live in /usr/include/minizip/. Passing -DASSIMP_BUILD_MINIZIP=ON
-- skips the pkgconfig lookup entirely and makes assimp compile its own
-- contrib/unzip/ instead.
--
-- This file shadows the xmake-repo assimp package (v6.x only).
-- Add patches here if older versions are ever needed.
package("assimp")
    set_homepage("https://assimp.org")
    set_description("Portable Open-Source library to import various well-known 3D model formats in a uniform manner")
    set_license("BSD-3-Clause")

    set_urls("https://github.com/assimp/assimp/archive/refs/tags/$(version).zip",
             "https://github.com/assimp/assimp.git")

    add_versions("v6.0.4", "1eeb63f3e6f6c9d820cc52f7d44fa6b6557256330f45ddaa903aa658c47fece5")
    add_versions("v6.0.3", "e9b3208513aa4566955a45cc085e031f7053e28f2e6a0e33d1657450bd0519c5")
    add_versions("v6.0.2", "699b455b92ce2b6b39aa06a957e59f9d83e8652c8b51364e811660a4acb9ee49")
    add_versions("v6.0.1", "24256974f66e36df6c72b78d4903e1bb6875b6d3f8aa8638639def68f2c50fd0")

    add_configs("build_tools",           {description = "Build the supplementary tools for Assimp.", default = false, type = "boolean"})
    add_configs("double_precision",      {description = "Enable double precision processing.", default = false, type = "boolean"})
    add_configs("no_export",             {description = "Disable Assimp's export functionality (reduces library size).", default = false, type = "boolean"})
    add_configs("android_jniiosysystem", {description = "Enable Android JNI IOSystem support.", default = false, type = "boolean"})
    add_configs("asan",                  {description = "Enable AddressSanitizer.", default = false, type = "boolean"})
    add_configs("ubsan",                 {description = "Enable Undefined Behavior sanitizer.", default = false, type = "boolean"})
    add_configs("draco",                 {description = "Enable Draco, primary for GLTF.", default = false, type = "boolean"})

    -- minizip dep removed: -DASSIMP_BUILD_MINIZIP=ON below makes assimp use
    -- its own contrib/unzip/, so no external minizip is needed.
    add_deps("cmake", "zlib")

    if is_plat("windows") then
        add_syslinks("advapi32")
    end

    if on_check then
        on_check("android", function (package)
            import("core.tool.toolchain")
            local ndk = toolchain.load("ndk", {plat = package:plat(), arch = package:arch()})
            local ndk_sdkver = ndk:config("ndk_sdkver")
            assert(ndk_sdkver and tonumber(ndk_sdkver) >= 26, "package(assimp): need ndk api level >= 26 for android")
        end)
    end

    on_load(function (package)
        if package:is_plat("linux", "macosx") and package:config("shared") then
            package:add("links", "assimp" .. (package:is_debug() and "d" or ""))
        end
    end)

    on_install(function (package)
        if package:is_plat("android") then
            import("core.tool.toolchain")
            local ndk = toolchain.load("ndk", {plat = package:plat(), arch = package:arch()})
            local ndk_sdkver = ndk:config("ndk_sdkver")
            assert(ndk_sdkver and tonumber(ndk_sdkver) >= 26, "package(assimp): need ndk api level >= 26 for android")
        end

        local configs = {
            "-DASSIMP_BUILD_SAMPLES=OFF",
            "-DASSIMP_BUILD_TESTS=OFF",
            "-DASSIMP_BUILD_DOCS=OFF",
            "-DASSIMP_BUILD_FRAMEWORK=OFF",
            "-DASSIMP_INSTALL_PDB=ON",
            "-DASSIMP_INJECT_DEBUG_POSTFIX=ON",
            "-DASSIMP_BUILD_ZLIB=OFF",
            "-DSYSTEM_IRRXML=ON",
            "-DASSIMP_WARNINGS_AS_ERRORS=OFF",
            -- Force assimp to compile its own contrib/unzip instead of
            -- searching for system minizip via pkgconfig. Without this,
            -- the system minizip pkgconfig sets UNZIP_FOUND=TRUE but
            -- UNZIP_INCLUDE_DIRS="" (no -I flag emitted for standard
            -- dirs), so #include <unzip.h> fails because the headers
            -- live in /usr/include/minizip/, not /usr/include/.
            "-DASSIMP_BUILD_MINIZIP=ON",
        }
        table.insert(configs, "-DCMAKE_BUILD_TYPE=" .. (package:debug() and "Debug" or "Release"))

        local function add_config_arg(config_name, cmake_name)
            table.insert(configs, "-D" .. cmake_name .. "=" .. (package:config(config_name) and "ON" or "OFF"))
        end
        add_config_arg("shared",           "BUILD_SHARED_LIBS")
        add_config_arg("double_precision", "ASSIMP_DOUBLE_PRECISION")
        add_config_arg("no_export",        "ASSIMP_NO_EXPORT")
        add_config_arg("asan",             "ASSIMP_ASAN")
        add_config_arg("ubsan",            "ASSIMP_UBSAN")
        add_config_arg("draco",            "ASSIMP_BUILD_DRACO")

        if package:is_plat("android") then
            add_config_arg("android_jniiosysystem", "ASSIMP_ANDROID_JNIIOSYSTEM")
        end
        if package:is_plat("windows", "linux", "macosx", "mingw") then
            add_config_arg("build_tools", "ASSIMP_BUILD_ASSIMP_TOOLS")
        else
            table.insert(configs, "-DASSIMP_BUILD_ASSIMP_TOOLS=OFF")
        end

        -- Suppress -Werror in CMakeLists (ASSIMP_WARNINGS_AS_ERRORS may not work for all versions)
        for _, cmakefile in ipairs(table.join("CMakeLists.txt", os.files("**/CMakeLists.txt"))) do
            if package:is_plat("windows") then
                io.replace(cmakefile, "/W4 /WX", "", {plain = true})
            else
                io.replace(cmakefile, "-Werror", "", {plain = true})
            end
        end

        if package:is_plat("windows") then
            -- fix ninja debug build pdb dir
            os.mkdir(path.join(package:builddir(), "code/pdb"))
            -- MDd == _DEBUG + _MT + _DLL: drop /D_DEBUG to avoid redefinition
            if package:is_debug() and package:has_runtime("MD", "MT") then
                io.replace("CMakeLists.txt", "/D_DEBUG", "", {plain = true})
            end
            -- fix std::min/max conflict with windows.h
            io.insert("code/AssetLib/IFC/IFCLoader.cpp", 1, "#define NOMINMAX")
        end

        -- Pass xmake-built zlib paths explicitly so cmake's FindZLIB picks it up
        local zlib = package:dep("zlib")
        if zlib and not zlib:is_system() then
            local fetchinfo = zlib:fetch({external = false})
            if fetchinfo then
                local includedirs = fetchinfo.includedirs or fetchinfo.sysincludedirs
                if includedirs and #includedirs > 0 then
                    table.insert(configs, "-DZLIB_INCLUDE_DIR=" .. table.concat(includedirs, " "))
                end
                local libfiles = fetchinfo.libfiles
                if libfiles then
                    table.insert(configs, "-DZLIB_LIBRARY=" .. table.concat(libfiles, " "))
                end
            end
        end

        if package:is_plat("linux") then
            -- cmake auto-detects /usr/bin/c++ (GCC) on Ubuntu; force clang so that
            -- -stdlib=libc++ is accepted. LLVM_PATH is set by install-llvm-action
            -- on CI; fall back to PATH-resident clang on local dev (e.g. Arch Linux).
            local llvm_path = os.getenv("LLVM_PATH")
            local clangxx = llvm_path and path.join(llvm_path, "bin", "clang++") or "clang++"
            local clang   = llvm_path and path.join(llvm_path, "bin", "clang")   or "clang"
            table.insert(configs, "-DCMAKE_CXX_COMPILER=" .. clangxx)
            table.insert(configs, "-DCMAKE_C_COMPILER="   .. clang)
            -- Match the libc++ stdlib used by the rest of the project on Linux+LLVM
            -- to avoid C++ ABI mismatches when linking assimp into the executables.
            table.insert(configs, "-DCMAKE_CXX_FLAGS=-stdlib=libc++")
            table.insert(configs, "-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++")
            table.insert(configs, "-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++")
        end

        import("package.tools.cmake").install(package, configs)

        if package:is_plat("windows") then
            if package:config("shared") then
                os.trycp(path.join(package:builddir(), "bin", "**.pdb"), package:installdir("bin"))
            else
                os.trycp(path.join(package:builddir(), "lib", "**.pdb"), package:installdir("lib"))
            end
        end
    end)

    on_test(function (package)
        local configs = {languages = "c++17"}
        if package:is_plat("linux") then
            -- libassimp.a is compiled with -stdlib=libc++; the link step must also
            -- pull in libc++ or std::__1 symbols from the .a are left unresolved.
            configs.cxxflags = "-stdlib=libc++"
            configs.ldflags  = "-stdlib=libc++"
        end
        assert(package:check_cxxsnippets({test = [[
            #include <cassert>
            void test() {
                Assimp::Importer importer;
            }
        ]]}, {configs = configs, includes = "assimp/Importer.hpp"}))
    end)
