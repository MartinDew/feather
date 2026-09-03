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
-- Identifies the flags the parse was last run with.
--
-- The mtime comparison below only notices changed headers, so editing the
-- ignore lists or any other parser flag would otherwise leave a stale api.json
-- in place and the change would appear to do nothing -- a genuinely confusing
-- failure, since the bindings then disagree with the flags that are right
-- there in the source.
function parser_flags_id()
    local parts = {}
    for _, f in ipairs(api_parser_flags()) do
        table.insert(parts, f)
    end
    for _, f in ipairs(c_abi_parser_flags()) do
        table.insert(parts, f)
    end
    for _, f in ipairs(python_parser_flags()) do
        table.insert(parts, f)
    end
    return hash.strhash128(table.concat(parts, "\0"))
end

function parser_flags_stamp_path()
    return path.join(output_dir(), ".parser_flags")
end

-- True when the parse's flags differ from the ones its outputs were produced
-- with (or were never recorded).
function parser_flags_changed()
    local stamp = parser_flags_stamp_path()
    if not os.isfile(stamp) then
        return true
    end
    return io.readfile(stamp):trim() ~= parser_flags_id()
end

function write_parser_flags_stamp()
    io.writefile(parser_flags_stamp_path(), parser_flags_id())
end

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

-- The clang (or clang++) MRBind itself was built with, named `binname`
-- ("clang" or "clang++"). Parsing needs plain clang's -resource-dir or it
-- fails with cryptic errors (mrbind's docs/running_parser.md); the Python
-- module has to be *compiled and linked* by the same install
-- (docs/generating_python.md) -- linking needs clang++ specifically, so that
-- the driver auto-links the C++ runtime library even though the link step's
-- only inputs are pre-compiled .o files (see configure_python_target).
-- Mirrors thirdparty/packages/mrbind.lua's own on_load clang resolution,
-- which builds the same "clang"/"clang++" pair from the same llvm_config or
-- libllvm data: primary path reads the data mrbind.lua stashed on the
-- package instance, with an independent fallback since cross-package
-- package:data() readability from a *consuming* target isn't guaranteed.
function _resolve_clang_binary(target, binname)
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
        clang = path.join(path.directory(llvm_config), binname .. suffix)
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
            clang = path.join(path.directory(tool.program), binname .. suffix)
        else
            -- No system llvm-config: mrbind fell back to building libllvm from
            -- source (always the case on Windows). target:pkg("mrbind"):dep("libllvm")
            -- isn't readable from a consuming target, so the target must
            -- add_packages("libllvm") itself (see thirdparty/xmake.lua) to get
            -- a usable handle here.
            local libllvm_pkg = target:pkg("libllvm")
            if libllvm_pkg then
                local bin = path.join(libllvm_pkg:installdir(), "bin", binname)
                clang = is_plat("windows") and (bin .. ".exe") or bin
            end
        end
    end

    assert(clang and os.isfile(clang),
        "feather_bindings: could not resolve the " .. binname .. " mrbind was built with")
    return clang
end

function resolve_clang(target)
    return _resolve_clang_binary(target, "clang")
end

function resolve_clangxx(target)
    return _resolve_clang_binary(target, "clang++")
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

-- DirectXMath's headers are parsed (SimpleMath's vector types keep their fields
-- in XMFLOAT bases), so their filenames need a --map-path of their own: every
-- parsed filename must match some prefix or mrbind_gen_c refuses to run.
--
-- Published in the API metadata as directxmath_root, because a plugin's
-- generator has to reproduce the same mapping from the same api.json.
function directxmath_includedir(target)
    local pkg = assert(target:pkg("directxmath"),
        "feather_bindings: target must have the directxmath package (via feather_public_api)")
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

        -- Make the parse platform-neutral.
        --
        -- Integer types are recorded with the spelling they have on the machine
        -- that parsed, and the API description these flags feed is published
        -- for plugin projects to generate bindings from on *any* platform. Left
        -- alone, `size_t` is captured as `unsigned long` from a Linux parse,
        -- and the resulting C header declares `unsigned long *` where Windows
        -- needs `unsigned long long *` -- a hard compile error in the generated
        -- glue, and a silent ABI mismatch anywhere it is not.
        --
        -- Canonicalizing to the fixed-width typedefs records `uint64_t`
        -- instead, which resolves to the right underlying type on each
        -- platform. Caught by cross-compiling to mingw, where feather::RID's
        -- `size_t id` failed exactly this way.
        "--canonicalize-to-fixed-size-typedefs",

        -- Internal plumbing that happens to live under core/: the shared body
        -- of the two extension loaders (core/resources/extension_loading.h).
        -- It is not part of the API a plugin talks to, and being unexported it
        -- cannot be linked from outside the engine at all.
        "--ignore", "feather::resolve_and_register_extension_entry",
        -- Likewise the resolved layout of a scripted component
        -- (core/world/scripted_component.h). Its fields are std::function
        -- accessors, which have no C or C# spelling; a language binding
        -- describes a component and lets the engine lay it out, rather than
        -- handling the closures the engine built to read it.
        "--ignore", "feather::ScriptedFieldLayout",
        "--skip-mentions-of", "feather::ScriptedFieldLayout",
        "--ignore", "feather::ScriptedComponentLayout",
        "--skip-mentions-of", "feather::ScriptedComponentLayout",
        -- --skip-mentions-of does not reach a function that only names the type
        -- through a pointer return, so the lookups go by name as well.
        "--ignore", "feather::find_scripted_component",
        -- Same for what a scripted system's callback is handed
        -- (core/world/scripted_system.h): raw component storage plus the
        -- ClassInfo describing it, and a std::function to call. The registering
        -- side of that API is meant for the engine's own language hosts.
        "--ignore", "feather::ScriptedSystemComponent",
        "--skip-mentions-of", "feather::ScriptedSystemComponent",
        "--ignore", "feather::ScriptedSystemInvocation",
        "--skip-mentions-of", "feather::ScriptedSystemInvocation",
        "--ignore", "feather::register_scripted_system",
        -- StaticString's implicit conversions and comparisons don't survive
        -- the trip to C#. std::string_view maps to ReadOnlySpan<char>, a ref
        -- struct, and the generator's IEquatable implementation for
        -- operator==(std::string_view) instantiates Nullable<ReadOnlySpan<char>>,
        -- which C# forbids outright; the two conversion operators
        -- (std::string_view and std::string) additionally collapse into a
        -- duplicate conversion and a duplicate ToString(). None of that is
        -- fixable from this side, so the operators are dropped and the
        -- equivalent named accessors -- str(), data(), size(), hash() -- carry
        -- the same information in every language.
        "--ignore", "/nassimp::StaticString::operator.*/",

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

        -- Third-party types reachable from core's headers. SimpleMath is
        -- handled separately, per-parse: the C ABI binds it (see
        -- c_abi_parser_flags), the Python one does not.
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

-- Flags that apply only to the parse the C ABI is generated from (the JSON
-- one). They exist to let SimpleMath's math types cross the C boundary, which
-- is what gives a C or C++ plugin a Transform with a position.
--
-- Only the six types core actually uses (core/math/math_defs.h) are admitted,
-- not all of SimpleMath.
function c_abi_parser_flags()
    return {
        -- Exposed structs take their fields from their bases, and the parser
        -- only copies base members when asked. Also what lets a derived class
        -- offer its inherited methods in C.
        "--copy-inherited-members",

        -- --ignore "::" above blacklists everything outside feather/nassimp, so
        -- the math types need admitting by name.
        --
        -- The optional member tail is load-bearing: these patterns are matched
        -- whole, so a pattern naming only the class admits the class and
        -- rejects every one of its members -- including the constructors and
        -- destructor, without which mrbind treats the type as not
        -- constructible or destructible and refuses to return one at all.
        --
        -- Only constructors and destructors are admitted (the tail matches the
        -- class's own name, optionally with a "~"), never a general "::.*":
        -- SimpleMath defines most of its methods out of line in SimpleMath.inl,
        -- and admitting an out-of-line member definition walks into a parser
        -- assertion -- the enclosing class is not on its stack there. The
        -- arithmetic is not wanted anyway; a plugin calls its own copy.
        "--allow", "/DirectX::SimpleMath::(Vector2|Vector3|Vector4|Quaternion|Color|Matrix)"
            .. "(::~?(Vector2|Vector3|Vector4|Quaternion|Color|Matrix))?/",
        -- Their fields live in these bases, and a base the parse never saw is
        -- dropped from the class entirely -- leaving something with no members
        -- and no destructor, which cannot be bound at all.
        --
        -- XMFLOAT4X4 (Matrix's base) holds an anonymous union, which the parser
        -- refuses to record as a field. That only rules Matrix out of
        -- --expose-as-struct, not out of the bindings: it stays an opaque class
        -- and crosses the ABI as a pointer.
        "--allow", "/DirectX::XMFLOAT[234](::.*)?/",
        -- The class only, without the "(::.*)?" tail the others carry: its
        -- members live in an anonymous union, and admitting those crashes the
        -- parser, which never pushes an entity for the anonymous record to
        -- attach them to. Matrix is opaque, so only its own members matter.
        "--allow", "DirectX::XMFLOAT4X4",

        -- The SIMD types the math methods take and return. Nothing can pass an
        -- __m128 through a C ABI, so every method mentioning one drops out --
        -- which is most of SimpleMath's arithmetic. The wrapper does not need
        -- them: it aliases these types to the plugin's own SimpleMath copy and
        -- calls its operators directly, in the plugin.
        -- The XMFLOAT matrix shapes and the packed formats appear only in
        -- Matrix's and Color's four out-of-line constructors, which must stay
        -- rejected for the reason given above.
        "--skip-mentions-of", "/DirectX::(XMVECTOR[A-Z0-9]*|XMMATRIX|[FGHC]XMVECTOR|[FC]XMMATRIX|XMFLOAT[34]X[34]|XM(U?INT)[234])/",
        -- XMVECTOR is a typedef for a compiler vector type, and matching is by
        -- canonical spelling, so the name above never catches it off MSVC.
        "--skip-mentions-of", "/.*__vector_size__.*/",
        "--skip-mentions-of", "/DirectX::PackedVector::.*/",

        -- The comparison categories a defaulted operator<=> returns. They have
        -- no C spelling, and DirectXMath's structs default their comparisons.
        "--skip-mentions-of", "/std::(partial|weak|strong)_ordering/",

        -- Members are deliberately NOT ignored: mrbind decides whether a class
        -- can be default-constructed, copied or assigned by looking at the
        -- constructors it parsed, and an opaque Matrix needs those to be
        -- constructible and returnable at all.
    }
end

-- Exclusions that apply only to the Python parse. Pybind11 has opinions the C
-- backend doesn't, and since Python is parsed separately anyway (it needs the
-- macro format), these cost nothing to keep out of the shared set -- the C and
-- C# bindings keep everything here.
function python_parser_flags()
    return {
        -- Pybind11 has no exposed-struct equivalent, and binding SimpleMath
        -- through it was never part of the Python surface.
        "--skip-mentions-of", "/DirectX::SimpleMath::.*/",
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
    -- Lets a header hide a declaration from the bindings with FEATHER_NO_BIND
    -- (core/framework/export_defs.h) while the compiler still sees it. --ignore
    -- does not reach static data members, which is what forced the attribute.
    table.insert(argv, "-DFEATHER_MRBIND_PARSE=1")
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

-- The mrbind revision thirdparty/packages/mrbind.lua pins. Duplicated as a
-- plain string because a package's URL spec isn't reachable from here; the
-- assertion in the export task is that these two stay equal.
function mrbind_pinned_commit()
    return "232ff33159d5e76e57b11669453d7d25ad22a14d"
end

-- Paths inside api.json and in the exported metadata are always spelled with
-- forward slashes, so every flag derived from them has to be too -- on Windows
-- they would otherwise be translated to backslashes and stop matching.
function to_forward_slashes(p)
    return (tostring(p):gsub("\\", "/"))
end

function dist_dir()
    return output_dir("dist")
end

function dist_api_json_path()
    return path.join(dist_dir(), "feather_api.json")
end

function dist_api_meta_path()
    return path.join(dist_dir(), "feather_api.meta.json")
end

-- Identifies the ABI-shaping flags run_gen_c() passes, so a plugin project can
-- tell whether its own copy of the generator flags still matches the engine
-- that produced the API file it was given. Published as gen_c_flags_id in the
-- exported metadata; tools/SDK/FeatherPluginSDK.lua computes the same value
-- from its own flag list and refuses to build when the two disagree.
--
-- Only the flags that shape the generated ABI go in. Input and output paths
-- are per-build and mean nothing to a consumer.
--
-- KEEP IN SYNC with run_gen_c() below and with FeatherPluginSDK.lua's
-- gen_c_flags_id().
-- Deliberately hashes only the *shape* of the flags -- the prefixes, the path
-- mappings' targets, the helper directory -- and none of the paths themselves.
--
-- feather_root is data carried in the metadata, identical on both sides by
-- construction, so including it detects nothing. It actively breaks things: it
-- is an absolute path, and hashing it through path.join() made the result
-- depend on the host separator, so a Windows plugin build computed a different
-- id than the Linux engine that exported the file and failed with a drift error
-- describing a disagreement that did not exist.
--
-- KEEP IN SYNC with FeatherPluginSDK's modules/feather_plugin_bindings.lua.
function gen_c_flags_id()
    local shape = {
        "helper-name-prefix=Feather_",
        "helper-macro-name-prefix=FEATHER_C_",
        "map-path=<root>/core->feather_c",
        "map-path=<root>->feather_c/_root",
        "assume-include-dir=<root>",
        "force-emit-common-helpers",
        "helper-header-dir=feather_helpers",
        -- Placeholder, like the <root> entries above: the mapping's shape is
        -- what must agree between engine and plugin, never the absolute path.
        "map-path=<directxmath>->feather_c/_ext/directxmath",
        "assume-include-dir=<directxmath>",
    }
    for _, t in ipairs(exposed_struct_types()) do
        table.insert(shape, "expose-as-struct=" .. t)
    end
    return hash.strhash128(table.concat(shape, "\0"))
end

-- The math types a C++ plugin defines itself rather than reaching through a
-- wrapper: it compiles the same SimpleMath sources the engine did, so the
-- generator aliases these and asserts the published layout.
--
-- Matrix is here too even though it is not an exposed struct: it still crosses
-- as itself, just through a pointer to a copy.
-- KEEP IN SYNC with native_math_types() in the SDK's
-- tools/SDK/modules/feather_plugin_bindings.lua.
function native_math_types()
    return {
        "DirectX::SimpleMath::Vector2",
        "DirectX::SimpleMath::Vector3",
        "DirectX::SimpleMath::Vector4",
        "DirectX::SimpleMath::Quaternion",
        "DirectX::SimpleMath::Color",
        "DirectX::SimpleMath::Matrix",
    }
end

-- The C++ types emitted as real C structs rather than opaque pointers, so they
-- cross the ABI by value with a layout a consumer can rely on.
--
-- Only the union-free SimpleMath types qualify: Matrix's XMFLOAT4X4 base holds
-- an anonymous union, which the parser refuses to record as a field, so it
-- stays opaque and is copied through a pointer instead.
-- KEEP IN SYNC with the SDK's shape_flags() and gen_c_argv() in
-- tools/SDK/modules/feather_plugin_bindings.lua.
function exposed_struct_types()
    return {
        "DirectX::SimpleMath::Vector2",
        "DirectX::SimpleMath::Vector3",
        "DirectX::SimpleMath::Vector4",
        "DirectX::SimpleMath::Quaternion",
        "DirectX::SimpleMath::Color",
    }
end

-- mrbind_gen_c's exception-relaying helper (__mrbind_c_details.cpp) calls
-- typeid() but doesn't include <typeinfo> for it -- <exception> happens to
-- drag it in transitively under libstdc++, so this only surfaces under
-- clang-cl on Windows ("member access into incomplete type 'const
-- type_info'"). Patching the generator's own output rather than adding a
-- force-include to the target: the force-include would apply to every
-- generated source, and this bug is specific to the one file mrbind writes it
-- into.
function _fixup_missing_typeinfo_include(source_dir)
    local details_cpp = path.join(source_dir, "__mrbind_c_details.cpp")
    if not os.isfile(details_cpp) then
        return
    end
    local content = io.readfile(details_cpp)
    if content:find("#include <typeinfo>", 1, true) then
        return
    end
    -- Prefer anchoring next to <exception>, the header whose absence this is
    -- actually working around; fall back to right after the file's own
    -- generated header include, which every version of this file starts with.
    local patched, n = content:gsub("(#include <exception>)", "%1\n#include <typeinfo>", 1)
    if n == 0 then
        patched, n = content:gsub("(#include \"__mrbind_c_details%.h\")", "%1\n#include <typeinfo>", 1)
    end
    if n > 0 then
        io.writefile(details_cpp, patched)
    else
        cprint("${yellow}[c_bindings]${reset} could not patch missing <typeinfo> include into %s"
            .. " -- mrbind's generated file layout may have changed", details_cpp)
    end
end

-- Content-compare copy of a generated tree. A file whose bytes are unchanged
-- keeps its mtime, so a regeneration that produced identical output doesn't
-- make xmake rebuild the generated glue and every consumer of the headers.
-- Files that vanished from `src` are removed from `dst` -- the job
-- --clean-output-dirs used to do, now that the generator writes to a staging
-- dir instead of straight into the build.
local function _sync_tree(src, dst)
    local kept = {}
    for _, f in ipairs(os.files(path.join(src, "**"))) do
        local rel = path.relative(f, src)
        kept[rel] = true
        local into = path.join(dst, rel)
        if not os.isfile(into) or io.readfile(into) ~= io.readfile(f) then
            os.mkdir(path.directory(into))
            os.cp(f, into)
        end
    end
    for _, f in ipairs(os.files(path.join(dst, "**"))) do
        if not kept[path.relative(f, dst)] then
            os.rm(f)
        end
    end
end

local function _sync_file(src, dst)
    if not os.isfile(dst) or io.readfile(dst) ~= io.readfile(src) then
        os.mkdir(path.directory(dst))
        os.cp(src, dst)
    end
end

local function _list_gen_c_sources(source_dir)
    local sources = os.files(path.join(source_dir, "**.cpp"))
    if #sources == 0 then
        -- The generator is documented to emit .cpp implementation files; fall
        -- back to .c in case a given version differs.
        sources = os.files(path.join(source_dir, "**.c"))
    end
    return sources
end

-- A generator only needs to re-run when its input file's contents or its flags
-- change. A stamp holding a hash of both, written after a successful run, lets
-- an unchanged rebuild skip the generator outright -- more robust than an mtime
-- comparison, which a `touch` or a byte-identical re-export would defeat.
local function _gen_stamp_value(input_file, flags_id)
    return hash.sha256(input_file) .. ":" .. flags_id
end

local function _gen_outputs_fresh(stamp_path, input_file, flags_id, present)
    if not present then
        return false
    end
    return os.isfile(stamp_path)
        and io.readfile(stamp_path):trim() == _gen_stamp_value(input_file, flags_id)
end

local function gen_c_stamp_path()
    return path.join(output_dir("c"), ".gen_c_stamp")
end

-- Generates the C bindings from api.json into opts.header_dir/opts.source_dir,
-- and returns every generated implementation file.
function run_gen_c(target, opts)
    opts = opts or {}
    local header_dir = assert(opts.header_dir, "run_gen_c: opts.header_dir required")
    local source_dir = assert(opts.source_dir, "run_gen_c: opts.source_dir required")
    local feather_root = assert(opts.feather_root, "run_gen_c: opts.feather_root required")

    -- Nothing that feeds the C bindings changed: skip the generator entirely.
    if _gen_outputs_fresh(gen_c_stamp_path(), api_json_path(), gen_c_flags_id(),
            os.isfile(c_desc_json_path()) and #_list_gen_c_sources(source_dir) > 0) then
        return _list_gen_c_sources(source_dir)
    end

    -- mrbind_gen_c rewrites every output file on every run. Generate into a
    -- staging tree, then copy across only the files that actually differ (see
    -- _sync_tree), so an unchanged regeneration leaves mtimes -- and the
    -- downstream build -- untouched.
    local stage = path.join(output_dir("c"), ".stage")
    local stage_headers = path.join(stage, "include")
    local stage_sources = path.join(stage, "src")
    local stage_desc = path.join(stage, "desc.json")
    os.tryrm(stage)
    os.mkdir(stage_headers)
    os.mkdir(stage_sources)

    local argv = {
        "--input", api_json_path(),
        "--output-header-dir", stage_headers,
        "--output-source-dir", stage_sources,
        "--helper-name-prefix", opts.helper_prefix or "Feather_",
        -- NOT "FEATHER_": the generated exports.h would then define
        -- FEATHER_API, a name core/framework/export_defs.h used to define
        -- unconditionally. A generated .cpp includes both, so on Windows the
        -- generated functions would be declared dllimport at their own
        -- definitions (or the two definitions would just collide). Harmless on
        -- ELF, where both spellings expand to the same visibility attribute.
        "--helper-macro-name-prefix", opts.helper_macro_prefix or "FEATHER_C_",
        -- The prefix a C consumer includes through: core/main/init_level.h
        -- becomes <feather_c/main/init_level.h>. The "core" segment is mapped
        -- away rather than kept because it names an engine source layout the
        -- consumer has no reason to know about.
        --
        -- This spelling deliberately differs from --assume-include-dir's
        -- spelling of the ORIGINAL headers below. With both set to
        -- feather_root, a generated .cpp's two includes -- quoted for its own
        -- generated header, angled for the real C++ one -- would resolve to
        -- the identical relative path "core/x/y.h", leaving resolution to
        -- depend on -I order alone (mrbind's docs/generating_c.md warns about
        -- exactly this collision). With this prefix the two spellings are
        -- textually distinct.
        --
        -- Longer prefixes win (--map-path's documented rule), so the second
        -- mapping only catches parsed headers from outside core/. There are
        -- none today (the parse covers core/ alone, see xmake/bindings.lua),
        -- but every parsed filename must match some prefix or the generator
        -- errors out, so it stays as a backstop.
        -- Built by concatenation, not path.join(): path.join() translates to
        -- the host separator, which on Windows would spell a prefix that no
        -- longer matches the filenames recorded inside api.json. The consumer
        -- SDK derives the same strings the same way.
        "--map-path", to_forward_slashes(feather_root) .. "/core", "feather_c",
        "--map-path", to_forward_slashes(feather_root), "feather_c/_root",
        -- Parsed from outside the engine tree entirely; see
        -- directxmath_includedir.
        "--map-path", to_forward_slashes(directxmath_includedir(target)), "feather_c/_ext/directxmath",
        "--assume-include-dir", to_forward_slashes(feather_root),
        -- The glue includes the real <DirectXMath.h> to call into it. Distinct
        -- from the mapping above, which spells the generated header instead.
        "--assume-include-dir", to_forward_slashes(directxmath_includedir(target)),
        "--clean-output-dirs",
        "--output-desc-json", stage_desc,
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
    -- Math types cross by value as real structs; see exposed_struct_types.
    for _, t in ipairs(exposed_struct_types()) do
        table.insert(argv, "--expose-as-struct")
        table.insert(argv, t)
    end
    for _, f in ipairs(opts.extra_flags or {}) do
        table.insert(argv, f)
    end

    cprint("${cyan}[c_bindings]${reset} mrbind_gen_c -> %s", path.relative(header_dir, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind_gen_c"), argv)
    _fixup_missing_typeinfo_include(stage_sources)

    os.mkdir(header_dir)
    os.mkdir(source_dir)
    _sync_tree(stage_headers, header_dir)
    _sync_tree(stage_sources, source_dir)
    -- desc.json feeds the C# generator; only replace it when it changed, so an
    -- unchanged C parse doesn't cascade into a C# regeneration.
    _sync_file(stage_desc, c_desc_json_path())
    os.tryrm(stage)

    io.writefile(gen_c_stamp_path(), _gen_stamp_value(api_json_path(), gen_c_flags_id()))
    return _list_gen_c_sources(source_dir)
end

-- The flags feather_gen_cpp needs beyond its input and output paths. Shared so
-- the engine's own generation and a plugin's cannot disagree about which types
-- the consumer defines itself.
-- KEEP IN SYNC with the SDK's gen_cpp_argv().
function gen_cpp_shape_flags()
    local argv = {}
    for _, t in ipairs(native_math_types()) do
        table.insert(argv, "--native-type")
        table.insert(argv, t)
        table.insert(argv, "SimpleMath.h")
    end
    -- The engine spells these unqualified in its own headers (core/math/math_defs.h);
    -- a plugin gets the same spellings.
    table.insert(argv, "--native-alias-namespace")
    table.insert(argv, "feather")
    return argv
end

-- Generates the C++ wrappers from the C generator's descriptor into
-- opts.output_dir, and copies in the hand-written headers that go with them.
function run_gen_cpp(target, opts)
    opts = opts or {}
    local output_dir = assert(opts.output_dir, "run_gen_cpp: opts.output_dir required")
    local sdk_cpp_dir = assert(opts.sdk_cpp_dir, "run_gen_cpp: opts.sdk_cpp_dir required")

    local desc_json = c_desc_json_path()
    assert(os.isfile(desc_json),
        "feather_bindings: " .. desc_json .. " is missing -- the C bindings must be generated first")

    local flags = gen_cpp_shape_flags()
    local flags_id = hash.strhash128(table.concat(flags, "\0"))

    local stamp = output_dir .. ".stamp"
    if _gen_outputs_fresh(stamp, desc_json, flags_id,
            #os.files(path.join(output_dir, "**.hpp")) > 0) then
        return output_dir
    end

    -- Staged and content-synced for the same reason as the other generators: an
    -- unchanged regeneration must not bump mtimes on the emitted headers.
    local stage = output_dir .. ".stage"
    os.tryrm(stage)
    os.mkdir(stage)

    local argv = {"--input-json", desc_json, "--output-dir", stage, "--clean-output-dir"}
    for _, f in ipairs(flags) do
        table.insert(argv, f)
    end

    cprint("${cyan}[cpp_bindings]${reset} feather_gen_cpp -> %s", path.relative(output_dir, os.projectdir()))
    os.vrunv(mrbind_bin(target, "feather_gen_cpp"), argv)

    -- The hand-written half of the surface: the plugin entry point macro and
    -- the ECS registration API, which describe a plugin rather than the engine
    -- and so are not generated.
    for _, f in ipairs(os.files(path.join(sdk_cpp_dir, "feather_cpp", "*.hpp"))) do
        os.cp(f, path.join(stage, "feather_cpp", path.filename(f)))
    end

    os.mkdir(output_dir)
    _sync_tree(stage, output_dir)
    os.tryrm(stage)

    io.writefile(stamp, _gen_stamp_value(desc_json, flags_id))
    return output_dir
end

-- Generates the C# bindings from the C generator's descriptor into
-- opts.output_dir.
function run_gen_csharp(target, opts)
    opts = opts or {}
    local output_dir = assert(opts.output_dir, "run_gen_csharp: opts.output_dir required")

    local desc_json = c_desc_json_path()
    assert(os.isfile(desc_json),
        "feather_bindings: " .. desc_json .. " is missing -- the C bindings must be generated first")

    -- Skip the generator when its input (the C descriptor) is byte-for-byte
    -- what produced the current output. The lib name is the only caller-varied
    -- flag that reaches the output.
    local csharp_flags_id = opts.imported_lib_name or "feather_c"
    local stamp = output_dir .. ".stamp"
    if _gen_outputs_fresh(stamp, desc_json, csharp_flags_id,
            #os.files(path.join(output_dir, "**.cs")) > 0) then
        return output_dir
    end

    -- Staged and content-synced for the same reason as the C generator: an
    -- unchanged regeneration must not bump mtimes on the emitted .cs files.
    -- A sibling of output_dir, not a child, so the sync walk never sees it.
    local stage = output_dir .. ".stage"
    os.tryrm(stage)
    os.mkdir(stage)

    local argv = {
        "--input-json", desc_json,
        "--output-dir", stage,
        -- The name the generated [DllImport]s carry. It names no file on disk:
        -- the C bindings are compiled into the engine executable
        -- (modules/c_bindings/xmake.lua), so a plugin's DllImportResolver maps
        -- this name onto the running process instead of loading anything.
        "--imported-lib-name", opts.imported_lib_name or "feather_c",
        -- C# has no free functions, so the generator puts helpers in a static
        -- class; the C++-style spelling here becomes Feather.Misc.* in C#.
        "--helpers-namespace", "Feather::Misc",
        -- No --force-namespace: the C++ side already lives in namespace
        -- `feather`, which the generator maps to `Feather` on its own. Forcing
        -- it as well produced `Feather.Feather.X` -- and worse, the generated
        -- IEquatable implementations still named the single-level spelling, so
        -- the output did not compile at all.
        "--clean-output-dir",
    }
    for _, f in ipairs(opts.extra_flags or {}) do
        table.insert(argv, f)
    end

    cprint("${cyan}[cs_bindings]${reset} mrbind_gen_csharp -> %s", path.relative(output_dir, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind_gen_csharp"), argv)

    os.mkdir(output_dir)
    _sync_tree(stage, output_dir)
    os.tryrm(stage)

    io.writefile(stamp, _gen_stamp_value(desc_json, csharp_flags_id))
    return output_dir
end

-- Where the Python that will import this module keeps its headers. Asked of
-- the interpreter itself rather than of python3-config, which is a separate
-- binary that can belong to a different installation entirely: a pybind11
-- module built against one minor version won't import into another, so the two
-- must not be allowed to disagree.
local function _python_include_dirs()
    import("lib.detect.find_tool")

    local tool = assert(find_tool("python3") or find_tool("python"),
        "feather_bindings: no python3 on PATH")

    local script = [[
import sysconfig
p = sysconfig.get_paths()
print(p['include'])
print(p['platinclude'])
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

    return includes
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
            -- Exactly one fragment carries the shared implementation. The
            -- value matters: MRBind tests this with #if, and a macro defined
            -- to nothing makes that an empty expression ("expected value in
            -- expression"). -DMB_DEFINE_IMPLEMENTATION on a command line would
            -- have been 1 implicitly; written out, it has to say so.
            table.insert(lines, "#define MB_DEFINE_IMPLEMENTATION 1")
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
    -- whatever the project's own toolchain is. Linking has to go through the
    -- clang++ driver specifically, not clang: the link step's only inputs are
    -- pre-compiled .o files, and plain clang has no source file in the
    -- command line to infer a C++ link from, so it silently skips linking the
    -- C++ runtime library -- the module then fails to import with an
    -- undefined libstdc++ RTTI symbol (__si_class_type_info's vtable).
    -- clang++ always links it, regardless of input file types.
    local clang = resolve_clang(target)
    local clangxx = resolve_clangxx(target)
    target:set("toolset", "cc", clang)
    target:set("toolset", "cxx", clangxx)
    target:set("toolset", "ld", clangxx)
    target:set("toolset", "sh", clangxx)

    for _, dir in ipairs(_python_include_dirs()) do
        target:add("includedirs", dir)
    end
    -- Python itself is deliberately not linked: a module's Python symbols
    -- resolve against the interpreter that loads it, which is what lets one
    -- module work with both a static and a shared libpython. (Windows, which
    -- would have to link the import library instead, doesn't build this module
    -- at all -- see modules/py_bindings/xmake.lua.)
    if is_plat("macosx") then
        -- Mach-O rejects undefined symbols in a dylib by default.
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
