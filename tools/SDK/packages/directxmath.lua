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
        -- os.scriptdir() is <sdk>/packages, so the vendored sal.h is one level
        -- up. Not os.projectdir(): a downstream consumer's build would resolve
        -- that to the wrong repo.
        local sal_src = path.join(path.directory(os.scriptdir()), "thirdparty", "DirectXMath", "sal.h")
        if os.isfile(sal_src) then
            os.cp(sal_src, dst_inc)
        end
    end)
package_end()
