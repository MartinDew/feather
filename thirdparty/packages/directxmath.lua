-- DirectXMath (header-only) with a local sal.h shim for non-MSVC platforms.
package("directxmath_feather")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/microsoft/DirectXMath")
    set_urls("https://github.com/microsoft/DirectXMath.git", {tag = "apr2025"})

    -- Declared here so xmake resolves it from installdir without an on_fetch() override.
    add_includedirs("include")

    on_install(function(package)
        local dst_inc = package:installdir("include")
        os.mkdir(dst_inc)
        if os.isdir("Inc") then
            os.cp(path.join("Inc", "*.h"),   dst_inc)
            os.cp(path.join("Inc", "*.inl"), dst_inc)
        end
        -- os.scriptdir() here is thirdparty/packages/; go up two levels to
        -- the engine root. Deliberately not os.projectdir(): this package
        -- can be resolved by a downstream consumer's build (via
        -- tools/SDK/FeatherSDK.lua), whose top-level project root is NOT the
        -- engine's -- os.projectdir() would silently point at the wrong repo.
        local sal_src = path.join(path.directory(path.directory(os.scriptdir())), "thirdparty", "DirectXMath", "sal.h")
        if os.isfile(sal_src) then
            os.cp(sal_src, dst_inc)
        end
    end)
package_end()
