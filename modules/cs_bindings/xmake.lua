-- C# bindings for the public API. They're generated from the C bindings
-- rather than from api.json directly: mrbind_gen_csharp reads the descriptor
-- mrbind_gen_c emits, and the C# it produces calls the C shared library
-- through [DllImport].
--
-- The deliverable is the generated C# source under build/bindings/csharp,
-- which a consumer compiles into its own assembly with its own .NET SDK --
-- this repo has no .NET toolchain and doesn't need one. At runtime its
-- [DllImport]s resolve against the C bindings compiled into the engine binary
-- (see modules/c_bindings), so it has to run inside an engine process.

if not has_config("enable_cs_bindings") then
    return
end

-- Nothing to generate from: the C generator's descriptor is the input, and the C# these produce calls the C bindings at runtime.
-- A note rather than an error, since both options default on and turning one off is a deliberate act.
if not has_config("enable_c_bindings") then
    -- print(), not cprint(): only script scope (on_load, on_config, ...) has
    -- the colour-printing interface.
    print("[cs_bindings] skipped: C# bindings are generated from the C ones, and enable_c_bindings is off")
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "csharp")

-- Phony: the output is C# source, which nothing in this build compiles.
-- c_bindings writes the descriptor this reads, and on_config follows dependency
-- order, so depending on that module orders the two generators.
feather_module_target("cs_bindings", os.scriptdir(), {},
    {kind = "phony", deps = {"c_bindings"}})

target("cs_bindings")
    add_packages("mrbind")

    on_config(function (target)
        import("feather_bindings")
        feather_bindings.run_gen_csharp(target, {output_dir = OUTPUT_DIR})
    end)
target_end()
