option("enable_py_bindings")
    set_default(false)
    set_description("Generate Python bindings for Feather's public API via MRBind (not yet implemented; the mrbind package doesn't build a Python generator)")
option_end()

if not has_config("enable_py_bindings") then
    return
end

-- Scaffolding only: thirdparty/packages/mrbind.lua only sets
-- -DMRBIND_BUILD_GENERATOR_C=ON and -DMRBIND_BUILD_GENERATOR_CSHARP=ON --
-- no mrbind_gen_python binary exists yet (upstream's Python backend needs
-- pybind11 and a different build path, not just another CMake flag flip).
-- A phony target (rather than no target at all) means enabling this option
-- is visibly "not yet implemented" instead of silently doing nothing.
target("py_bindings")
    set_kind("phony")
    set_group("bindings")
    on_load(function (target)
        cprint("${yellow}[py_bindings]${reset} not yet implemented -- enable_py_bindings is scaffolding only;"
            .. " thirdparty/packages/mrbind.lua doesn't build a Python generator"
            .. " (would need -DMRBIND_BUILD_GENERATOR_PYTHON=ON plus pybind11, not wired up)")
    end)
target_end()
