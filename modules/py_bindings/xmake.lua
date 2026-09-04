-- Python bindings for the public API, as an importable extension module
-- (build/bindings/python/feather.so).
--
-- The Python backend works differently from the C and C# ones: MRBind ships no
-- Python generator binary. The parser instead emits language-agnostic macros
-- (build/bindings/api_macros.cpp, see xmake/bindings.lua), and that file is
-- compiled against MRBind's own pybind11 target header, which expands them
-- into pybind11 registration calls. So "generating" the Python bindings means
-- compiling one enormous translation unit.
--
-- Compiling it whole can exhaust memory, so MRBind splits it: the same file is
-- compiled once per fragment with a different MB_FRAGMENT and the objects are
-- linked together. Each fragment gets a small generated .cpp that sets its
-- macros and includes the macro file, which keeps the compiling and linking
-- inside xmake -- it knows what else the module has to link, which a hand-rolled
-- link line would have to reproduce.
--
-- The one thing xmake can't do here is choose the compiler per target the way
-- this needs: the translation unit must be compiled by the same Clang that
-- built MRBind, whatever the project's toolchain is, because the generated code
-- leans on template machinery other compilers choke on (mrbind's
-- docs/generating_python.md). set_toolset() below points this target at it.
--
-- Like the C bindings, and unlike an ordinary Python extension, this module
-- does NOT link the engine. It is imported by an interpreter embedded in a
-- running engine process (modules/py_host), so its engine symbols stay
-- undefined and bind to that process when Python dlopens it -- the executable
-- links with -rdynamic for exactly this.
--
-- That direction is what makes the ClassDB problem go away. Linking the engine
-- in gave a module with its own copy of every engine global and an empty
-- reflection registry, because register_*_types() runs from Main::setup_db() in
-- feather_main.cpp, which feather_core leaves out. A script running inside the
-- engine needs none of that: setup_db() has already run, and the singletons the
-- bindings reach are the live ones.
--
-- The cost is that `import feather` no longer works from a plain interpreter --
-- there is no engine process for it to bind against. Running scripts through
-- the engine (a .fext manifest of type "python") is the supported path.

if not has_config("enable_py_bindings") then
    return
end

-- The platform check lives here rather than in the option's default, where
-- is_plat() reads as false whatever the platform is (see xmake/options.lua).
if is_plat("windows", "mingw") then
    -- print(), not cprint(): only script scope (on_load, on_config, ...) has
    -- the colour-printing interface.
    print("[py_bindings] skipped: the module has to be built by a Clang in MSVC-compatible mode"
        .. " against the official Python's ABI, which this repo's Windows toolchain setup"
        .. " doesn't cover yet")
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "python")
local FRAGMENT_DIR = path.join(OUTPUT_DIR, "fragments")
local MODULE_NAME = "feather"
local NUM_FRAGMENTS = 4

target("py_bindings")
    set_kind("shared")
    set_group("bindings")
    -- Python finds an extension module by filename, so this has to be exactly
    -- the module name: feather.so, not libfeather.so.
    set_basename(MODULE_NAME)
    set_prefixname("")
    set_extension(".so")
    set_targetdir(OUTPUT_DIR)
    -- Generated code: there is nothing to act on in its warnings.
    set_warnings("none")

    -- Declared here rather than from on_config: xmake resolves a target's source files before on_config runs, and a file added
    -- later is rejected outright. always_added covers their not existing until the first build.
    for i = 0, NUM_FRAGMENTS - 1 do
        add_files(path.join(FRAGMENT_DIR, "fragment_" .. i .. ".cpp"), {always_added = true})
    end

    -- bindings_api's on_config writes api_macros.cpp, and on_config follows
    -- dependency order.
    add_deps("bindings_api")
    add_packages("mrbind", "pybind11")

    -- Headers only for the static packages: the engine's headers need them to compile, but linking their archives would give
    -- this module a second copy of state the engine process already owns. flecs is a real shared library, so it links normally.
    add_deps("feather_public_api")
    add_deps("simplemath")
    add_packages("flecs")
    add_packages("assimp", {links = {}})
    add_packages("sdl3", {links = {}})
    add_packages("taywee_args")

    -- Same defines the engine compiles itself with: the bindings call into it
    -- directly, so their view of its headers has to match.
    if is_mode("debug", "releasedbg") then
        add_defines("BETA")
    end
    if is_mode("release") then
        add_defines("PRODUCTION")
    end

    -- Shipped next to the engine binary, where modules/py_host adds it to sys.path -- the same arrangement as libfeather_c:
    -- the engine carries what its extensions and scripts bind against.
    after_build(function (target)
        local outdir = path.join(FEATHER_ROOT, "build", "bin", "python")
        os.mkdir(outdir)
        os.vcp(target:targetfile(), outdir)
    end)

    on_config(function (target)
        import("feather_bindings")

        feather_bindings.write_python_fragments(target, {
            fragment_dir = FRAGMENT_DIR,
            module_name = MODULE_NAME,
            num_fragments = NUM_FRAGMENTS,
        })
        feather_bindings.configure_python_target(target, {module_name = MODULE_NAME})
    end)
target_end()
