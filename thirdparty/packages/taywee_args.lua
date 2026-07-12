-- taywee/args: single-header C++ argument parser.
-- If "args" ever appears in xrepo (xrepo search args), switch to it and delete this file.
package("taywee_args")
    set_kind("library", {headeronly = true})
    set_homepage("https://github.com/Taywee/args")
    set_urls("https://github.com/Taywee/args.git", {tag = "6.4.7"})

    add_includedirs("include")
    add_defines("ARGS_NOEXCEPT")

    on_load(function(package)
        package:add("defines", "ARGS_NOEXCEPT")
    end)

    on_install(function(package)
        os.cp("args.hxx", package:installdir("include"))
    end)
package_end()

add_requires("taywee_args 6.4.7", {system = false, alias = "taywee_args"})
