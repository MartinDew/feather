-- MRBind pipeline helpers, shared by the feather.mrbind_api rule (which runs
-- the parser once for the whole build) and by the three bindings modules that
-- consume its output -- see modules/{c,cs,py}_bindings/xmake.lua.
--
-- Must be an import()-able module, not plain xmake.lua globals: on_load/
-- before_build sandboxes can't see description-scope globals (feather_codegen.lua's
-- header comment documents the same constraint).

-- Everything a consumer project needs lands here, one subdirectory per
-- language, alongside the api.json every generator reads.
function output_dir(subdir)
    local root = path.join(os.projectdir(), "build", "bindings")
    return subdir and path.join(root, subdir) or root
end

function api_json_path()
    return path.join(output_dir(), "api.json")
end

-- The Python backend consumes macros rather than JSON (mrbind has no Python
-- generator binary; see modules/py_bindings/xmake.lua).
function api_macros_path()
    return path.join(output_dir(), "api_macros.cpp")
end

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
        -- before **), not "**/*.h" -- the latter silently matches nothing.
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
-- relative to feather_root (matching how modules already include core headers,
-- e.g. <core/framework/reflected.h>). Write-if-changed so an unaffected
-- rebuild doesn't touch the file's mtime and force a redundant re-parse,
-- mirroring generate_reflection.py's pattern.
function generate_combined_header(output_path, feather_root, dirs)
    local headers = list_headers(dirs)

    local lines = {"#pragma once"}
    for _, h in ipairs(headers) do
        table.insert(lines, "#include <" .. path.relative(h, feather_root) .. ">")
    end
    local content = table.concat(lines, "\n") .. "\n"

    local changed = not os.isfile(output_path) or io.readfile(output_path) ~= content
    if changed then
        os.mkdir(path.directory(output_path))
        io.writefile(output_path, content)
    end
    return output_path, changed
end

-- True when any of `outputs` is missing or older than the newest header under
-- `dirs`. Generated headers count here even though they're kept out of the
-- combined header: the parse reads them through their originating header, so a
-- reflection-codegen refresh has to invalidate the parse too.
function outputs_are_stale(outputs, dirs)
    local newest = 0
    for _, dir in ipairs(dirs) do
        for _, pattern in ipairs({"**.h", "**.hpp"}) do
            for _, f in ipairs(os.files(path.join(dir, pattern))) do
                local mtime = os.mtime(f)
                if mtime > newest then
                    newest = mtime
                end
            end
        end
    end

    for _, output in ipairs(outputs) do
        if not os.isfile(output) or os.mtime(output) < newest then
            return true
        end
    end
    return false
end

-- The clang MRBind itself was built with. Parsing needs that exact clang's
-- -resource-dir or it fails with cryptic errors (mrbind's docs/running_parser.md),
-- and the Python module has to be compiled by it too (docs/generating_python.md).
-- Mirrors thirdparty/packages/mrbind.lua's own on_load clang resolution:
-- primary path reads the data it stashed on the package instance, with an
-- independent fallback since cross-package package:data() readability from a
-- *consuming* target isn't guaranteed.
function resolve_clang(target)
    import("lib.detect.find_tool")

    local mrbind_pkg = target:pkg("mrbind")
    local clang

    -- A consuming target's package handle has no :data() method at all in
    -- some xmake versions -- data_set() from the package's own on_load isn't
    -- readable here, so check the method exists before calling it rather than
    -- assuming it and crashing on_load.
    local llvm_config = mrbind_pkg and mrbind_pkg.data and mrbind_pkg:data("llvm_config")
    if llvm_config then
        local suffix = mrbind_pkg:data("llvm_suffix") or ""
        clang = path.join(path.directory(llvm_config), "clang" .. suffix)
    else
        -- mrbind.lua's on_load skips the system llvm-config search entirely on
        -- Windows, always using libllvm there -- mirror that exactly rather
        -- than searching PATH for a tool that path never takes.
        local tool
        local suffix = ""
        if not is_plat("windows") then
            tool = find_tool("llvm-config", {
                -- Homebrew's llvm is keg-only; mirrors mrbind.lua's own search.
                paths = {"/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"},
            })
            if not tool then
                for v = 30, 18, -1 do
                    tool = find_tool("llvm-config", {program = "llvm-config-" .. v})
                    if tool then
                        suffix = "-" .. v
                        break
                    end
                end
            end
        end
        if tool then
            clang = path.join(path.directory(tool.program), "clang" .. suffix)
        else
            -- No system llvm-config: mrbind fell back to building libllvm from
            -- source (always the case on Windows). target:pkg("mrbind"):dep("libllvm")
            -- isn't readable from a consuming target, so the target must
            -- add_packages("libllvm") itself (see thirdparty/xmake.lua) to get
            -- a usable handle here.
            local libllvm_pkg = target:pkg("libllvm")
            if libllvm_pkg then
                local bin = path.join(libllvm_pkg:installdir(), "bin", "clang")
                clang = is_plat("windows") and (bin .. ".exe") or bin
            end
        end
    end

    assert(clang and os.isfile(clang),
        "feather_bindings: could not resolve the clang mrbind was built with")
    return clang
end

function resolve_clang_resource_dir(target)
    return os.iorunv(resolve_clang(target), {"-print-resource-dir"}):trim()
end

-- Fully resolved include/define/std flags for `target` (feather_public_api,
-- its packages, ...) -- the same machinery behind this repo's
-- compile_commands.json rule -- instead of re-deriving -I/-D flags by hand.
-- Raw, NOT safe to hand to mrbind as-is: see sanitize_compflags_for_mrbind().
function resolve_compile_flags(target)
    import("core.tool.compiler")
    local inst = compiler.load("cxx", {target = target})
    return inst:compflags({target = target})
end

-- mrbind's clang frontend always parses its argv in plain GNU dialect (it's
-- built via a bare "clang"/"clang++", never "clang-cl" -- see
-- thirdparty/packages/mrbind.lua), regardless of the dialect the PROJECT's
-- toolchain uses. On a clang-cl Windows build, resolve_compile_flags() returns
-- MSVC-style flags (-Zi, -std:c++latest, -external:I<dir>, -Od, -MTd, ...)
-- that mrbind rejects as "unknown argument": the malformed -std: flag gets
-- silently dropped, clang falls back to an old default C++ standard, and
-- std::span/std::byte/std::source_location then fail to resolve, cascading
-- into unrelated-looking parse errors.
--
-- None of the debug-info/PDB/runtime-library/optimization flags matter to a
-- parse or to a header-only compile, so rather than translating each MSVC
-- spelling 1:1, keep only -I/-D (identical in both dialects, in both their
-- "-Ipath" and separate "-I" "path" token forms -- xmake emits both, depending
-- on which package's includedirs are being expressed) and any already-GNU-style
-- -std=..., rewrite -external:I<dir> to -I<dir>, and drop everything else.
--
-- -std=c++2c is only appended when no usable -std= survived (i.e. only the
-- MSVC -std:c++latest spelling was present), NOT unconditionally: forcing
-- -std=c++2c on Linux, where the raw flags already carry a perfectly usable
-- -std=c++23, makes mrbind's own AST consumer crash outright.
function sanitize_compflags_for_mrbind(flags)
    local out = {}
    local has_std = false
    local i = 1
    while i <= #flags do
        local f = flags[i]
        if f == "-I" or f == "-D" or f == "-isystem" then
            -- "-isystem <dir>" is how compflags() expresses PUBLIC thirdparty
            -- package include dirs -- distinct from the plain "-I" <dir> pair
            -- and from "-I<dir>" concatenated for the project's own dirs. A
            -- valid GNU-dialect flag, kept as-is rather than translated.
            table.insert(out, f)
            if flags[i + 1] then
                table.insert(out, flags[i + 1])
                i = i + 1
            end
        elseif f:startswith("-I") or f:startswith("-D") then
            table.insert(out, f)
        elseif f:startswith("-std=") then
            table.insert(out, f)
            has_std = true
        elseif f:startswith("-external:I") then
            table.insert(out, "-I" .. f:sub(#"-external:I" + 1))
        end
        i = i + 1
    end
    if not has_std then
        table.insert(out, "-std=c++2c")
    end
    return out
end

function mrbind_bin(target, name)
    local pkg = assert(target:pkg("mrbind"),
        "feather_bindings: target must add_packages(\"mrbind\")")
    local suffix = is_plat("windows") and ".exe" or ""
    return path.join(pkg:installdir(), "bin", name .. suffix)
end

function mrbind_includedir(target)
    local pkg = assert(target:pkg("mrbind"),
        "feather_bindings: target must add_packages(\"mrbind\")")
    return path.join(pkg:installdir(), "include")
end

-- Entities excluded from the parse, for every language at once (the parser
-- runs once and every generator reads its output). Each exclusion is a type
-- mrbind can't currently express, not a deliberate choice about the API.
--
-- --ignore drops the entity itself; --skip-mentions-of additionally drops
-- every function that so much as names it. Most entries need both: ignoring a
-- class doesn't stop a function returning one from being bound.
function api_parser_flags()
    return {
        -- Only feather:: is bound, plus StaticString's namespace below.
        "--ignore", "::",
        "--allow", "feather",
        -- StaticString (core/framework/static_string.hpp) is real Feather
        -- infrastructure used pervasively (ClassInfo, ClassDB, Reflected, ...),
        -- just namespaced "nassimp" rather than "feather" -- without its own
        -- --allow, mrbind refuses to bind anything that mentions it.
        "--allow", "nassimp",

        -- Standard containers that can't cross a C ABI. Matching is by
        -- spelling, and it doesn't cascade: skipping the element type doesn't
        -- skip a container of it (e.g. Projection::get_frustum_corners()'s
        -- std::array<Vector3, 8>), so each container needs its own regex, and
        -- bare "std::initializer_list" (no template args) matches nothing.
        "--skip-mentions-of", "/std::initializer_list<.*>/",
        "--skip-mentions-of", "/std::span<.*>/",
        "--skip-mentions-of", "/std::array<.*>/",
        "--skip-mentions-of", "/std::tuple<.*>/",
        "--skip-mentions-of", "/std::unordered_map<.*>/",

        -- Third-party types reachable from core's headers. Exposing
        -- SimpleMath's Vector2/3/4, Matrix, Quaternion and Color as real C
        -- structs (mrbind_gen_c's --expose-as-struct) is the obvious follow-up
        -- -- they're PODs of floats -- but is left out of this pass.
        "--skip-mentions-of", "/DirectX::SimpleMath::.*/",
        "--skip-mentions-of", "/flecs::.*/",
        "--skip-mentions-of", "/args::.*/",

        -- EngineSettings is a private-constructor singleton whose private
        -- `_settings` member is unordered_map<string_view, unique_ptr<...>>,
        -- making it implicitly non-copyable. mrbind still attempts a
        -- copy-constructor wrapper for the whole class (skipping the map field
        -- alone doesn't stop that), which fails to compile. Not constructible
        -- through bindings anyway.
        "--ignore", "feather::EngineSettings",
        "--skip-mentions-of", "feather::EngineSettings",

        -- Variant is a type-erasure wrapper over many alternatives (SimpleMath's
        -- among them): its per-alternative as<T>()/is<T>() surface needs traits
        -- for every alternative even when the individual functions are skipped,
        -- so the whole class needs --ignore.
        "--ignore", "feather::Variant",
        "--skip-mentions-of", "feather::Variant",
        -- VariantArray is built directly on Variant; --ignore doesn't cascade
        -- into nested types, so its iterators need their own entries.
        "--ignore", "feather::VariantArray",
        "--skip-mentions-of", "feather::VariantArray",
        "--ignore", "feather::VariantArray::iterator",
        "--skip-mentions-of", "feather::VariantArray::iterator",
        "--ignore", "feather::VariantArray::const_iterator",
        "--skip-mentions-of", "feather::VariantArray::const_iterator",
        -- ClassInfo carries a Variant-returning factory field -- same cascade.
        "--ignore", "feather::ClassInfo",
        "--skip-mentions-of", "feather::ClassInfo",

        -- Delegate<T>, instantiated exhaustively as bindings do, surfaces a
        -- genuine pre-existing template bug: class_db.h's
        -- `using subclass_delegate_t = Delegate<const std::string_view>` fails
        -- to compile delegate.h's forwarding call when actually instantiated,
        -- which normal engine code never triggers. Worth fixing separately.
        "--ignore", "feather::Delegate",
        "--skip-mentions-of", "feather::Delegate",
        -- Same story: StaticIndexedArray<T>::begin() const compares size_t
        -- against unsigned long long in std::max (distinct types on Linux,
        -- std::max needs identical ones) -- broken for any T, but nothing in
        -- the engine calls begin()/end() on one, so it's never instantiated.
        "--ignore", "feather::StaticIndexedArray",
        "--skip-mentions-of", "feather::StaticIndexedArray",
        -- consteval, so it exists only during compilation: there's no address
        -- to call at runtime and nothing for a binding to point at. A
        -- compile-time C++-type-to-VariantType mapping has no meaning in
        -- another language anyway.
        "--ignore", "feather::get_variant_type",
        "--skip-mentions-of", "feather::get_variant_type",

        -- Takes a raw function pointer (the extension entry point), which has
        -- no binding representation. Engine-internal plumbing: only the
        -- extension loader calls it, from C++ (core/main/init_level.h).
        "--ignore", "feather::register_extension_entry",
        "--skip-mentions-of", "feather::register_extension_entry",

        -- Declared in math_defs.h, never defined anywhere -- a dead
        -- declaration that only becomes a link error once a generated wrapper
        -- (which calls everything declared) is force-linked in.
        "--ignore", "feather::raise_to_next_multiple_of",
        "--skip-mentions-of", "feather::raise_to_next_multiple_of",
        -- RID::invalid()'s out-of-line constexpr definition (core/resources/rid.cpp)
        -- is only called from RID::is_valid() in that same file; in release
        -- builds GCC constant-folds that call away and elides the out-of-line
        -- definition, so no symbol exists for a binding to call. Debug and
        -- releasedbg are fine; only release hits this.
        "--ignore", "feather::RID::invalid",
        "--skip-mentions-of", "feather::RID::invalid",
    }
end

-- Exclusions that apply only to the Python parse. Pybind11 has opinions the C
-- backend doesn't, and since Python is parsed separately anyway (it needs the
-- macro format), these cost nothing to keep out of the shared set -- the C and
-- C# bindings keep everything here.
function python_parser_flags()
    return {
        -- StaticString's conversion operators to std::string and
        -- std::string_view. Pybind11 converts both of those to Python str
        -- through built-in casters rather than binding them as classes, and
        -- refuses to register a class for a type it already casts.
        "--ignore", "/nassimp::StaticString::operator .*/",
        -- LaunchSettings' constructor and init() take a `char **` argv, which
        -- has no Python representation -- and no purpose there either, since
        -- the interpreter owns the process's arguments.
        "--skip-mentions-of", "/char \\*\\*/",
        -- EntityRender holds its mesh and material in const members, so its
        -- copy assignment is deleted -- and pybind11's mutable sequence
        -- protocol, which MRBind binds CowVector<EntityRender> through, needs
        -- to assign elements. Deliberate immutability on the engine's side
        -- rather than something to fix, and a renderer's per-entity draw data
        -- is not a scripting surface.
        "--ignore", "feather::RenderScene::EntityRender",
        "--skip-mentions-of", "feather::RenderScene::EntityRender",
    }
end

-- Parses the combined header into `opts.output`. `opts.format` is "json" (the
-- API dump every generator but Python reads) or "macros" (what the Python
-- backend compiles). Returns the output path.
function run_parse(target, opts)
    opts = opts or {}
    local combined_header = assert(opts.combined_header, "run_parse: opts.combined_header required")
    local output = assert(opts.output, "run_parse: opts.output required")
    local format = opts.format or "json"

    local argv = {combined_header, "-o", output, "--format=" .. format}
    for _, f in ipairs(api_parser_flags()) do
        table.insert(argv, f)
    end
    for _, f in ipairs(opts.extra_parser_flags or {}) do
        table.insert(argv, f)
    end

    table.insert(argv, "--")
    table.insert(argv, "-xc++-header")
    table.insert(argv, "-resource-dir=" .. resolve_clang_resource_dir(target))
    for _, f in ipairs(sanitize_compflags_for_mrbind(resolve_compile_flags(target))) do
        table.insert(argv, f)
    end

    os.mkdir(path.directory(output))
    cprint("${cyan}[bindings]${reset} mrbind --format=%s -> %s", format, path.relative(output, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind"), argv)
    return output
end

-- Where mrbind_gen_c writes its machine-readable description of the C API.
-- The C# backend is built on top of the C one and reads this rather than
-- api.json (see modules/cs_bindings/xmake.lua).
function c_desc_json_path()
    return path.join(output_dir("c"), "desc.json")
end

-- Generates the C bindings from api.json into opts.header_dir/opts.source_dir,
-- and returns every generated implementation file.
function run_gen_c(target, opts)
    opts = opts or {}
    local header_dir = assert(opts.header_dir, "run_gen_c: opts.header_dir required")
    local source_dir = assert(opts.source_dir, "run_gen_c: opts.source_dir required")
    local feather_root = assert(opts.feather_root, "run_gen_c: opts.feather_root required")

    os.mkdir(header_dir)
    os.mkdir(source_dir)

    local argv = {
        "--input", api_json_path(),
        "--output-header-dir", header_dir,
        "--output-source-dir", source_dir,
        "--helper-name-prefix", opts.helper_prefix or "Feather_",
        -- NOT "FEATHER_": the generated exports.h would then define
        -- FEATHER_API, which core/framework/export_defs.h already defines
        -- unconditionally. A generated .cpp includes both, so on Windows the
        -- generated functions would be declared dllimport at their own
        -- definitions (or the two definitions would just collide). Harmless on
        -- ELF, where both spellings expand to the same visibility attribute.
        "--helper-macro-name-prefix", opts.helper_macro_prefix or "FEATHER_C_",
        -- OUT="generated" rather than "." deliberately differs from
        -- --assume-include-dir's spelling of the ORIGINAL headers. With both
        -- set to feather_root, a generated .cpp's two includes -- quoted for
        -- its own generated header, angled for the real C++ one -- would
        -- resolve to the identical relative path "core/x/y.h", leaving
        -- resolution to depend on -I order alone (mrbind's docs/generating_c.md
        -- warns about exactly this collision). With this prefix the two
        -- spellings are textually distinct.
        "--map-path", feather_root, "generated",
        "--assume-include-dir", feather_root,
        "--clean-output-dirs",
        "--output-desc-json", c_desc_json_path(),
        -- The C# bindings are generated from the descriptor above and need the
        -- common helpers header (C++-compatible allocation functions among
        -- other things) to exist whether or not the C bindings alone would
        -- have pulled it in. Always emitting it keeps the two generators'
        -- inputs consistent regardless of which languages are enabled.
        "--force-emit-common-helpers",
        -- Keeps the generated helper headers out of the top level of the
        -- include dir, where they'd sit among the mirrored engine headers.
        "--helper-header-dir", "feather_helpers",
    }
    for _, f in ipairs(opts.extra_flags or {}) do
        table.insert(argv, f)
    end

    cprint("${cyan}[c_bindings]${reset} mrbind_gen_c -> %s", path.relative(header_dir, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind_gen_c"), argv)

    local sources = os.files(path.join(source_dir, "**.cpp"))
    if #sources == 0 then
        -- The generator is documented to emit .cpp implementation files; fall
        -- back to .c in case a given version differs.
        sources = os.files(path.join(source_dir, "**.c"))
    end
    return sources
end

-- Generates the C# bindings from the C generator's descriptor into
-- opts.output_dir.
function run_gen_csharp(target, opts)
    opts = opts or {}
    local output_dir = assert(opts.output_dir, "run_gen_csharp: opts.output_dir required")

    local desc_json = c_desc_json_path()
    assert(os.isfile(desc_json),
        "feather_bindings: " .. desc_json .. " is missing -- the C bindings must be generated first")

    os.mkdir(output_dir)

    local argv = {
        "--input-json", desc_json,
        "--output-dir", output_dir,
        -- The C shared library the generated [DllImport]s load at runtime: no
        -- extension and no "lib" prefix, so this resolves to feather_c.dll,
        -- libfeather_c.so or libfeather_c.dylib. Must match
        -- modules/c_bindings/xmake.lua's set_basename().
        "--imported-lib-name", opts.imported_lib_name or "feather_c",
        -- C# has no free functions, so the generator puts helpers in a static
        -- class; the C++-style spelling here becomes Feather.Misc.* in C#.
        "--helpers-namespace", "Feather::Misc",
        "--force-namespace", "Feather",
        "--clean-output-dir",
    }
    for _, f in ipairs(opts.extra_flags or {}) do
        table.insert(argv, f)
    end

    cprint("${cyan}[cs_bindings]${reset} mrbind_gen_csharp -> %s", path.relative(output_dir, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind_gen_csharp"), argv)
    return output_dir
end

-- Where the Python that will import this module keeps its headers and import
-- library. Asked of the interpreter itself rather than of python3-config,
-- which is a separate binary that can belong to a different installation
-- entirely: a pybind11 module built against one minor version won't import
-- into another, so the two must not be allowed to disagree.
local function _python_config()
    import("lib.detect.find_tool")

    local tool = assert(find_tool("python3") or find_tool("python"),
        "feather_bindings: no python3 on PATH")

    local script = [[
import sysconfig, sys
p = sysconfig.get_paths()
print(p['include'])
print(p['platinclude'])
print(sysconfig.get_config_var('LIBDIR') or '')
print(sys.version_info[0], sys.version_info[1], sep='')
]]
    local out = os.iorunv(tool.program, {"-c", script})
    local lines = out:trim():split("\n", {strict = true})

    local includes = {lines[1]}
    if lines[2] and lines[2] ~= "" and lines[2] ~= lines[1] then
        table.insert(includes, lines[2])
    end
    assert(os.isfile(path.join(includes[1], "Python.h")),
        "feather_bindings: Python.h not found under " .. includes[1]
        .. " -- Python's development headers are required (python3-dev on Debian/Ubuntu)")

    return {includes = includes, libdir = lines[3], version = lines[4]}
end

-- Writes one small .cpp per fragment. Each sets its own MB_FRAGMENT and
-- includes the parser's macro output, which is what lets xmake compile and
-- link them as ordinary source files (see modules/py_bindings/xmake.lua).
function write_python_fragments(target, opts)
    opts = opts or {}
    local fragment_dir = assert(opts.fragment_dir, "write_python_fragments: opts.fragment_dir required")
    local num_fragments = opts.num_fragments or 4

    local macros_cpp = api_macros_path()
    assert(os.isfile(macros_cpp),
        "feather_bindings: " .. macros_cpp .. " is missing -- the parser must run first")

    os.mkdir(fragment_dir)
    for i = 0, num_fragments - 1 do
        local lines = {
            "// Generated by feather_bindings.write_python_fragments. Do not edit.",
            "#define MB_NUM_FRAGMENTS " .. num_fragments,
            "#define MB_FRAGMENT " .. i,
        }
        if i == 0 then
            -- Exactly one fragment carries the shared implementation.
            table.insert(lines, "#define MB_DEFINE_IMPLEMENTATION")
        end
        -- Absolute, so the fragment doesn't depend on the include path.
        table.insert(lines, "#include \"" .. macros_cpp .. "\"")

        local content = table.concat(lines, "\n") .. "\n"
        local fragment = path.join(fragment_dir, "fragment_" .. i .. ".cpp")
        -- Write-if-changed: rewriting would touch the mtime and force a
        -- recompile of a translation unit that takes minutes.
        if not os.isfile(fragment) or io.readfile(fragment) ~= content then
            io.writefile(fragment, content)
        end
    end
end

-- Points `target` at the Clang MRBind was built with and adds the flags the
-- generated pybind11 code needs. Called from the Python module's on_config,
-- which is the earliest point a toolset can be resolved.
function configure_python_target(target, opts)
    opts = opts or {}
    local module_name = opts.module_name or "feather"

    -- The generated code only compiles with the Clang that built MRBind,
    -- whatever the project's own toolchain is.
    local clang = resolve_clang(target)
    target:set("toolset", "cc", clang)
    target:set("toolset", "cxx", clang)
    target:set("toolset", "ld", clang)
    target:set("toolset", "sh", clang)

    local python = _python_config()
    for _, dir in ipairs(python.includes) do
        target:add("includedirs", dir)
    end
    -- Python itself is deliberately not linked on ELF or Mach-O: a module's
    -- Python symbols resolve against the interpreter that loads it, which is
    -- what lets one module work with both a static and a shared libpython.
    -- Windows has no such thing and must link the import library.
    if is_plat("windows") then
        assert(python.libdir ~= "", "feather_bindings: Python reported no LIBDIR to link against")
        target:add("linkdirs", path.join(python.libdir, "libs"))
        target:add("links", "python" .. python.version)
    elseif is_plat("macosx") then
        target:add("shflags", "-Wl,-undefined,dynamic_lookup", {force = true})
    end

    target:add("includedirs", mrbind_includedir(target))
    target:add("defines", "MRBIND_HEADER=<mrbind/targets/pybind11.h>")
    target:add("defines", "MB_PB11_MODULE_NAME=" .. module_name)
    -- Pybind11 shares internal state between modules built by the same
    -- compiler and ABI. Naming ours keeps that sharing to our own modules,
    -- which is what upstream recommends.
    target:add("defines", "PYBIND11_COMPILER_TYPE=\"_feather\"")
    target:add("defines", "PYBIND11_BUILD_ABI=\"_feather\"")

    -- Higher optimization makes this translation unit take considerably longer
    -- to compile for no meaningful gain: it's registration calls, not hot code.
    target:add("cxflags", "-O1", {force = true})
end
