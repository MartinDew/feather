-- C++ wrappers for the public API, generated from the C bindings' descriptor
-- by tools/SDK/gen_cpp -- the same input the C# generator reads.
--
-- The deliverable is header-only source under build/bindings/cpp, which a
-- plugin compiles with its own toolchain. Nothing in the engine uses it: the
-- engine has the real headers. What this target is for is proving, on every
-- build, that the generated headers still compile against nothing but the C
-- bindings and the vendored math -- the property that lets a plugin build with
-- no engine checkout at all.

if not has_config("enable_cpp_bindings") then
    return
end

-- Nothing to generate from: the C generator's descriptor is the input.
-- A note rather than an error, since both options default on and turning one
-- off is a deliberate act.
if not has_config("enable_c_bindings") then
    -- print(), not cprint(): only script scope has the colour-printing interface.
    print("[cpp_bindings] skipped: C++ wrappers are generated from the C ones, and enable_c_bindings is off")
    return
end

local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))
local OUTPUT_DIR = path.join(FEATHER_ROOT, "build", "bindings", "cpp")
local SDK_CPP_DIR = path.join(FEATHER_ROOT, "tools", "SDK", "cpp")
local C_INCLUDE_DIR = path.join(FEATHER_ROOT, "build", "bindings", "c", "include")

-- Phony: the output is header-only source, compiled by the check target below.
target("cpp_bindings")
    set_kind("phony")
    set_group("bindings")

    -- The C generation writes the descriptor this reads, and it runs as a rule
    -- on the engine target; on_config follows dependency order, so depending on
    -- the engine orders the two.
    add_deps("feather")
    add_packages("mrbind")

    on_config(function (target)
        import("feather_bindings")
        feather_bindings.run_gen_cpp(target, {
            output_dir = OUTPUT_DIR,
            sdk_cpp_dir = SDK_CPP_DIR,
            gen_cpp_dir = path.join(FEATHER_ROOT, "tools", "SDK", "gen_cpp"),
        })
    end)
target_end()

-- Compiles every generated header with only the C bindings, the vendored
-- SimpleMath and DirectXMath on the include path. Deliberately does NOT depend
-- on feather_public_api: reaching an engine header from here would defeat the
-- point of the check.
target("cpp_bindings_check")
    set_kind("object")
    set_group("bindings")
    set_languages("cxx23")
    set_warnings("none")

    add_deps("cpp_bindings")
    add_deps("simplemath", {inherit = false}) -- for its include dirs only
    add_packages("directxmath")

    add_includedirs(OUTPUT_DIR, C_INCLUDE_DIR,
        path.join(FEATHER_ROOT, "tools", "SDK", "thirdparty", "SimpleMath"))
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX")

    on_config(function (target)
        -- Written rather than committed: the set of headers follows whatever
        -- the generator emitted. Write-if-changed, so an unchanged build does
        -- not recompile it.
        local dir = path.join(target:autogendir(), "cpp_bindings_check")
        os.mkdir(dir)

        local lines = {
            "// Generated: compiles the C++ wrappers against the C bindings alone.",
            "#include <feather_cpp/assert.hpp>",
            "#include <feather_cpp/feather.hpp>",
            "#include <feather_cpp/plugin.hpp>",
            "#include <feather_cpp/scripted_abi.hpp>",
            "",
            "// The entry point macro is part of the surface, so it is instantiated too.",
            "static void _check_entry(feather::InitLevel) {}",
            "FEATHER_PLUGIN_ENTRY(feather_cpp_bindings_check_entry, _check_entry)",
        }
        local contents = table.concat(lines, "\n") .. "\n"

        local source = path.join(dir, "all_headers.cpp")
        if not os.isfile(source) or io.readfile(source) ~= contents then
            io.writefile(source, contents)
        end
        target:add("files", source, {always_added = true})
    end)
target_end()
