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
-- Unlike the C bindings, this module links the engine rather than loading into
-- a running one, so `import feather` works from a plain interpreter.

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

    -- Declared here rather than from on_config: xmake resolves a target's
    -- source files before on_config runs, and a file added later is rejected
    -- outright. always_added covers their not existing until the first build.
    for i = 0, NUM_FRAGMENTS - 1 do
        add_files(path.join(FRAGMENT_DIR, "fragment_" .. i .. ".cpp"), {always_added = true})
    end

    -- bindings_api's on_config writes api_macros.cpp, and on_config follows
    -- dependency order.
    add_deps("bindings_api")
    add_packages("mrbind", "pybind11")

    -- The engine, linked whole: a static library only hands over the members
    -- something already references, so anything reached indirectly (a virtual
    -- override, a factory registered from a static initializer) would be
    -- dropped from a module whose only references are the generated bindings.
    --
    -- What this does NOT do is populate the ClassDB: the register_*_types()
    -- calls run from Main::setup_db() in feather_main.cpp, which feather_core
    -- deliberately leaves out, and ClassDB's constructor is private to Main.
    -- So an imported module exposes the C++ API but starts with no reflection
    -- registry. Giving an embedder its own way in needs an engine-side entry
    -- point that doesn't exist yet -- a separate change from generating these
    -- bindings.
    add_deps("feather_core")
    add_packages("flecs", "assimp", "sdl3", "taywee_args")

    -- feather_core whole-archived, in the same group as the static libraries
    -- its objects call into (flecs_static, assimp, minizip, SDL3): a plain
    -- (non-whole-archive) static library is only scanned for members that
    -- resolve an undefined symbol already pending *at that point* in the
    -- link line, and for a shared-library target xmake places every
    -- package's -l flag before the target's own linkgroups regardless of
    -- declaration order -- confirmed empirically, and add_linkorders does
    -- nothing to it here (it only reorders a target's own links/linkgroups
    -- against each other, not package-derived ones). So when the linker
    -- reaches flecs_static et al., nothing has referenced them yet;
    -- whole-archiving feather_core afterwards forces in the objects that
    -- call them, but by then the linker has already moved past their
    -- archives and won't revisit them ("undefined symbol: EcsScopeClose" at
    -- import time -- a .so link doesn't fail outright on this the way an
    -- executable would, so it surfaces one symbol at a time via dlopen).
    -- Listing them alongside feather_core here puts them all in one
    -- explicit whole-archive block instead, sidestepping the ordering
    -- question entirely; xmake drops the packages' own plain -l flags for
    -- names that already appear in a linkgroup, so nothing double-links.
    add_linkgroups("feather_core", "flecs_static", "assimp", "minizip", "SDL3", {whole = true})

    -- No add_deps("simplemath") here, unlike the engine's other targets:
    -- simplemath is an object library, so its objects are already archived
    -- into libfeather_core.a, and whole-archive brings every one of them in.
    -- Depending on it as well would put the same objects in the link twice,
    -- which the linker rejects as multiple definitions.

    -- Same defines the engine compiles itself with: the bindings call into it
    -- directly, so their view of its headers has to match.
    add_defines("FEATHER_BUILDING_ENGINE")
    if is_mode("debug", "releasedbg") then
        add_defines("BETA")
    end
    if is_mode("release") then
        add_defines("PRODUCTION")
    end

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
