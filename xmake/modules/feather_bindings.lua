-- MRBind pipeline helpers shared by */bindings modules (see
-- modules/c_bindings/xmake.lua). Must be an import()-able module, not plain
-- xmake.lua globals: on_load/before_build sandboxes can't see
-- description-scope globals (see feather_codegen.lua's header comment for
-- the same constraint).

local function _is_generated(filepath)
    return filepath:endswith(".gen.h") or filepath:endswith(".gen.hpp")
end

-- Every *.h/*.hpp under `dirs`, recursively, excluding reflection-codegen
-- output (*.gen.h -- already #include-d by its originating header; including
-- it raw in a single-TU umbrella risks duplicate-symbol issues).
function list_headers(dirs)
    local headers = {}
    for _, dir in ipairs(dirs) do
        -- xmake's recursive-glob syntax is "**.h" (no separating slash
        -- before **), not "**/*.h" -- confirmed empirically, the latter
        -- silently matches nothing.
        for _, pattern in ipairs({"**.h", "**.hpp"}) do
            for _, f in ipairs(os.files(path.join(dir, pattern))) do
                if not _is_generated(f) then
                    table.insert(headers, f)
                end
            end
        end
    end
    table.sort(headers)
    return headers
end

-- Writes "#pragma once" + one #include<...> per header in `dirs`, paths
-- relative to feather_root (matches how modules already include core
-- headers, e.g. <core/framework/reflected.h>). Write-if-changed so an
-- unaffected rebuild doesn't touch the file's mtime and doesn't force a
-- redundant mrbind re-parse, mirroring generate_reflection.py's pattern.
function generate_combined_header(output_path, feather_root, dirs)
    local headers = list_headers(dirs)

    local lines = {"#pragma once"}
    for _, h in ipairs(headers) do
        table.insert(lines, "#include <" .. path.relative(h, feather_root) .. ">")
    end
    local content = table.concat(lines, "\n") .. "\n"

    if os.isfile(output_path) and io.readfile(output_path) == content then
        return output_path
    end
    os.mkdir(path.directory(output_path))
    io.writefile(output_path, content)
    return output_path
end

-- MRBind's parser needs -resource-dir=<clang -print-resource-dir> from the
-- SAME clang mrbind itself was built with, or parsing fails with cryptic
-- errors (see mrbind's docs/running_parser.md). Mirrors
-- thirdparty/packages/mrbind.lua's own on_load clang resolution; primary
-- path reads the data it stashed on the package instance, with an
-- independent fallback since cross-package package:data() readability from
-- a *consuming* target isn't verified for this xmake version.
function resolve_clang_resource_dir(target)
    import("lib.detect.find_tool")

    local mrbind_pkg = target:pkg("mrbind")
    local clang

    -- Confirmed empirically (xmake 3.1.0): a consuming target's package
    -- handle has no :data() method at all -- data_set() in the package's own
    -- on_load isn't readable from here, so check for the method's existence
    -- before calling it rather than assuming it and crashing on_load.
    local llvm_config = mrbind_pkg and mrbind_pkg.data and mrbind_pkg:data("llvm_config")
    if llvm_config then
        local suffix = mrbind_pkg:data("llvm_suffix") or ""
        clang = path.join(path.directory(llvm_config), "clang" .. suffix)
    else
        local tool = find_tool("llvm-config", {
            -- Homebrew's llvm is keg-only; mirrors mrbind.lua's own search.
            paths = {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"},
        })
        local suffix = ""
        if not tool then
            for v = 30, 18, -1 do
                tool = find_tool("llvm-config", {program = "llvm-config-" .. v})
                if tool then
                    suffix = "-" .. v
                    break
                end
            end
        end
        if tool then
            clang = path.join(path.directory(tool.program), "clang" .. suffix)
        elseif mrbind_pkg and mrbind_pkg.dep then
            -- mrbind fell back to building libllvm from source (no system
            -- llvm-config found) -- find clang in that package's installdir,
            -- same as mrbind.lua's on_install does for the static-build path.
            local libllvm = mrbind_pkg:dep("libllvm")
            if libllvm then
                clang = path.join(libllvm:installdir(), "bin", "clang")
            end
        end
    end

    assert(clang and os.isfile(clang),
        "feather_bindings: could not resolve the clang mrbind was built with")

    local resource_dir = os.iorunv(clang, {"-print-resource-dir"})
    return resource_dir:trim()
end

-- Fully resolved include/define/std flags for `target` (feather_public_api,
-- its packages, etc.) -- the same machinery behind this repo's
-- compile_commands.json rule -- spliced into the mrbind parse invocation
-- instead of re-deriving -I/-D flags by hand.
function resolve_compile_flags(target)
    import("core.tool.compiler")
    local inst = compiler.load("cxx", {target = target})
    return inst:compflags({target = target})
end

local function _mrbind_bin(mrbind_pkg, name)
    local suffix = is_plat("windows") and ".exe" or ""
    return path.join(mrbind_pkg:installdir(), "bin", name .. suffix)
end

-- Runs the full parse -> generate-C pipeline for `target`:
--   1. combined_input.h aggregating every header under opts.dirs
--   2. `mrbind` parse -> parsed.json (private, target:autogendir())
--   3. `mrbind_gen_c` generate -> opts.header_output_dir / opts.source_output_dir
-- Returns {header_dir, source_dir, sources} where `sources` is every
-- generated implementation file found under source_output_dir.
function run_c_pipeline(target, opts)
    opts = opts or {}
    local feather_root = assert(opts.feather_root, "feather_bindings.run_c_pipeline: opts.feather_root required")
    local dirs = assert(opts.dirs, "feather_bindings.run_c_pipeline: opts.dirs required")
    local header_output_dir = assert(opts.header_output_dir, "feather_bindings.run_c_pipeline: opts.header_output_dir required")
    local source_output_dir = assert(opts.source_output_dir, "feather_bindings.run_c_pipeline: opts.source_output_dir required")
    local helper_prefix = opts.helper_prefix or "Feather_"
    local helper_macro_prefix = opts.helper_macro_prefix or "FEATHER_"
    local allow_namespace = opts.allow_namespace or "feather"

    local mrbind_pkg = assert(target:pkg("mrbind"),
        "feather_bindings.run_c_pipeline: target must add_packages(\"mrbind\")")

    local autogendir = target:autogendir()
    local combined_header = generate_combined_header(
        path.join(autogendir, "combined_input.h"), feather_root, dirs)

    local resource_dir = resolve_clang_resource_dir(target)
    local compflags = resolve_compile_flags(target)

    local parsed_json = path.join(autogendir, "parsed.json")
    local parse_argv = {
        combined_header,
        "-o", parsed_json,
        "--ignore", "::",
        "--allow", allow_namespace,
    }
    for _, f in ipairs(opts.extra_parser_flags or {}) do
        table.insert(parse_argv, f)
    end
    table.insert(parse_argv, "--")
    table.insert(parse_argv, "-xc++-header")
    table.insert(parse_argv, "-resource-dir=" .. resource_dir)
    for _, f in ipairs(compflags) do
        table.insert(parse_argv, f)
    end

    cprint("${cyan}[c_bindings]${reset} mrbind (parse)")
    os.vrunv(_mrbind_bin(mrbind_pkg, "mrbind"), parse_argv)

    os.mkdir(header_output_dir)
    os.mkdir(source_output_dir)

    local gen_argv = {
        "--input", parsed_json,
        "--output-header-dir", header_output_dir,
        "--output-source-dir", source_output_dir,
        "--helper-name-prefix", helper_prefix,
        "--helper-macro-name-prefix", helper_macro_prefix,
        -- OUT="generated" (not ".") deliberately differs from
        -- --assume-include-dir's spelling of the ORIGINAL headers: with both
        -- set to feather_root, the generated .cpp's two #includes (quoted,
        -- for its own generated header; angled, for the real C++ header)
        -- resolve to the identical relative path "core/x/y.h", making
        -- resolution depend on -I order alone. Confirmed by hand (see
        -- mrbind's docs/generating_c.md's own collision warning) -- with
        -- this prefix the two spellings are textually distinct
        -- ("generated/core/x/y.h" vs "core/x/y.h"), safe regardless of -I
        -- order.
        "--map-path", feather_root, "generated",
        "--assume-include-dir", feather_root,
        "--clean-output-dirs",
        "--output-desc-json", path.join(header_output_dir, "desc.json"),
    }
    for _, f in ipairs(opts.extra_generator_flags or {}) do
        table.insert(gen_argv, f)
    end

    cprint("${cyan}[c_bindings]${reset} mrbind_gen_c (generate)")
    os.vrunv(_mrbind_bin(mrbind_pkg, "mrbind_gen_c"), gen_argv)

    local sources = os.files(path.join(source_output_dir, "**.cpp"))
    if #sources == 0 then
        -- mrbind_gen_c is documented to emit .cpp for the C++ implementation
        -- files; fall back to .c in case a given generator version differs.
        sources = os.files(path.join(source_output_dir, "**.c"))
    end

    return {
        header_dir = header_output_dir,
        source_dir = source_output_dir,
        sources = sources,
    }
end
