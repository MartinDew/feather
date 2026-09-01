-- py_host: an embedded CPython interpreter, so a project can ship extensions
-- written as Python scripts.
--
-- A .fext manifest of type "python" names a script rather than a shared
-- library; this module registers the runner that FextFormatLoader looks up for
-- that type (core/resources/script_extension_runner.h). Core itself never links
-- Python.
--
-- Scripts reach the engine through the `feather` module built by
-- modules/py_bindings, which binds to this process at import time. That module
-- is what makes this worth having, so the option is only useful alongside
-- enable_py_bindings.
--
-- Off by default: it links libpython, which not every build wants, and the
-- engine is perfectly usable without it.

option("enable_py_host")
    set_default(false)
    set_description("Embed a Python interpreter so .fext extensions of type \"python\" can run; ignored on Windows")
option_end()

-- The platform check lives here rather than in the option's default, where
-- is_plat() reads as false whatever the platform is (see xmake/options.lua).
if is_plat("windows", "mingw") then
    return
end

if not has_config("enable_py_host") then
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))

feather_module_target("py_host", os.scriptdir(), {
    "py_host.cpp",
    "py_ecs.cpp",
    "register_module.cpp",
})

target("py_host")
    -- py_ecs.cpp defines an embedded module with pybind11.
    add_packages("pybind11")

    -- The shim a script imports. Ships beside the engine binary in the same
    -- directory py_host puts on sys.path, next to the generated feather module.
    after_build(function (target)
        local outdir = path.join(FEATHER_ROOT, "build", "bin", "python")
        os.mkdir(outdir)
        os.vcp(path.join(os.scriptdir(), "python", "feather_ecs.py"), outdir)
    end)
target_end()

target("py_host")
    on_config(function (target)
        import("lib.detect.find_tool")

        -- python3-embed, not python3: the plain .pc omits the interpreter
        -- library itself, which is exactly what embedding needs.
        local pkgconfig = find_tool("pkg-config")
        if not pkgconfig then
            raise("py_host: pkg-config is required to locate an embeddable Python")
        end

        local function query(flag)
            local out = try { function ()
                return os.iorunv(pkgconfig.program, {flag, "python3-embed"})
            end }
            if not out then
                raise("py_host: pkg-config found no python3-embed; install your distribution's "
                    .. "Python development package (python3-dev / python3-devel)")
            end
            return out:trim()
        end

        for _, flag in ipairs(query("--cflags"):split("%s+")) do
            target:add("cxflags", flag, {force = true})
        end
        for _, flag in ipairs(query("--libs"):split("%s+")) do
            target:add("ldflags", flag, {force = true})
        end
    end)
target_end()

-- The module is a static library linked into the executable, so the
-- executable's own link line needs libpython too.
target("feather")
    on_config(function (target)
        import("lib.detect.find_tool")

        local pkgconfig = find_tool("pkg-config")
        if not pkgconfig then
            return
        end
        local out = try { function ()
            return os.iorunv(pkgconfig.program, {"--libs", "python3-embed"})
        end }
        if not out then
            return
        end
        for _, flag in ipairs(out:trim():split("%s+")) do
            target:add("ldflags", flag, {force = true})
        end
    end)
target_end()
