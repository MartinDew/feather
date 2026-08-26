-- C# bindings for the public API. They're generated from the C bindings
-- rather than from api.json directly: mrbind_gen_csharp reads the descriptor
-- mrbind_gen_c emits, and the C# it produces calls the C shared library
-- through [DllImport].
--
-- The deliverable is the generated C# source under build/bindings/csharp,
-- which a consumer compiles into its own assembly with its own .NET SDK --
-- this repo has no .NET toolchain and doesn't need one. At runtime that
-- assembly loads the feather_c library built by modules/c_bindings, so it has
-- to run inside an engine process (see that module's header comment).

if not has_config("enable_cs_bindings") then
    return
end

if not has_config("enable_c_bindings") then
    -- Nothing to generate from: the C generator's descriptor is the input.
    raise("enable_cs_bindings requires enable_c_bindings (C# bindings call the C ones)")
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "csharp")

-- Phony: the output is C# source, which nothing in this build compiles.
target("cs_bindings")
    set_kind("phony")
    set_group("bindings")

    -- c_bindings' on_config writes the descriptor this reads, and on_config
    -- follows dependency order.
    add_deps("c_bindings")
    add_packages("mrbind")

    on_config(function (target)
        import("feather_bindings")
        feather_bindings.run_gen_csharp(target, {output_dir = OUTPUT_DIR})
    end)
target_end()
