-- MRBind pipeline helpers, shared by the feather.mrbind_api rule (which runs
-- the parser once for the whole build) and by the three bindings modules that
-- consume its output -- see modules/{c,cs,cpp}_bindings/xmake.lua.
--
-- Must be an import()-able module, not plain xmake.lua globals: on_load/
-- before_build sandboxes can't see description-scope globals (feather_codegen.lua's
-- header comment documents the same constraint).

-- Everything a consumer project needs lands here, one subdirectory per language, alongside the api.json every generator reads.
function output_dir(subdir)
    local root = path.join(os.projectdir(), "build", "bindings")
    return subdir and path.join(root, subdir) or root
end

function api_json_path()
    return path.join(output_dir(), "api.json")
end

local function _is_generated(filepath)
    return filepath:endswith(".gen.h") or filepath:endswith(".gen.hpp")
end

-- Every *.h/*.hpp under `dirs`, excluding reflection-codegen output (*.gen.h, already #include-d by its originating header; raw in one TU risks duplicates).
function list_headers(dirs)
    local headers = {}
    for _, dir in ipairs(dirs) do
        -- xmake's recursive-glob syntax is "**.h" (no separating slash before **), not "**/*.h" -- the latter silently matches nothing.
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

-- Writes "#pragma once" + one #include per header in `dirs`, relative to feather_root.
-- Write-if-changed so an unaffected rebuild doesn't force a redundant re-parse.
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

-- Hash of the parser flags, so a flags-only edit (invisible to the header-mtime
-- check below) still invalidates a stale api.json instead of leaving it in place.
function parser_flags_id()
    local parts = {}
    for _, f in ipairs(api_parser_flags()) do
        table.insert(parts, f)
    end
    for _, f in ipairs(c_abi_parser_flags()) do
        table.insert(parts, f)
    end
    return hash.strhash128(table.concat(parts, "\0"))
end

function parser_flags_stamp_path()
    return path.join(output_dir(), ".parser_flags")
end

-- True when the parse's flags differ from the ones its outputs were produced with (or were never recorded).
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

-- The clang MRBind itself was built with; the parse needs its -resource-dir.
-- Mirrors mrbind.lua's own clang resolution, with an independent PATH fallback since a consuming target's package:data() isn't always readable.
function _resolve_clang_binary(target, binname)
    import("lib.detect.find_tool")

    local mrbind_pkg = target:pkg("mrbind")
    local clang

    -- Some xmake versions give a consuming target's package handle no :data() method at all, so check it exists before calling it.
    local llvm_config = mrbind_pkg and mrbind_pkg.data and mrbind_pkg:data("llvm_config")
    if llvm_config then
        local suffix = mrbind_pkg:data("llvm_suffix") or ""
        clang = path.join(path.directory(llvm_config), binname .. suffix)
    else
        -- mrbind.lua skips the system llvm-config search on Windows entirely, always using libllvm -- mirrored here exactly.
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
            -- No system llvm-config: mrbind built libllvm from source (always true on Windows), so the target must
            -- add_packages("libllvm") itself (thirdparty/xmake.lua) to get a usable handle here.
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

function resolve_clang_resource_dir(target)
    return os.iorunv(resolve_clang(target), {"-print-resource-dir"}):trim()
end

-- Fully resolved include/define/std flags for `target`, via the same machinery behind this repo's compile_commands.json rule.
-- Raw -- NOT safe to hand to mrbind as-is, see sanitize_compflags_for_mrbind().
function resolve_compile_flags(target)
    import("core.tool.compiler")
    local inst = compiler.load("cxx", {target = target})
    return inst:compflags({target = target})
end

-- mrbind's clang frontend always parses in GNU dialect, but a clang-cl build's MSVC-style compflags() are silently rejected (e.g. -std:c++latest), cascading into unrelated parse errors.
-- Keeps only -I/-D/-isystem and a GNU -std=, translates -external:I, drops the rest; appends -std=c++2c only when no -std= survived (forcing it over Linux's real -std=c++23 crashes mrbind).
function sanitize_compflags_for_mrbind(flags)
    local out = {}
    local has_std = false
    local i = 1
    while i <= #flags do
        local f = flags[i]
        if f == "-I" or f == "-D" or f == "-isystem" then
            -- "-isystem <dir>" is how compflags() expresses PUBLIC thirdparty include dirs; already valid GNU dialect, kept as-is.
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

-- Content hash of the C++ generator's sources, matching thirdparty/packages/mrbind.lua's gen_cpp_rev config. KEEP IN SYNC with the copy there.
function feather_gen_cpp_rev(dir)
    if not os.isdir(dir) then
        return ""
    end
    local files = os.files(path.join(dir, "**"))
    table.sort(files)
    local parts = {}
    for _, f in ipairs(files) do
        table.insert(parts, path.relative(f, dir) .. ":" .. hash.sha256(f))
    end
    return hash.strhash128(table.concat(parts, "\0"))
end

-- DirectXMath is parsed too (SimpleMath's fields live in XMFLOAT bases) so its headers need their own --map-path.
-- Published as directxmath_root so a plugin's own generator can reproduce the same mapping.
function directxmath_includedir(target)
    local pkg = assert(target:pkg("directxmath"),
        "feather_bindings: target must have the directxmath package (via feather_public_api)")
    return path.join(pkg:installdir(), "include")
end

-- Entities excluded from the parse for every language at once -- each exclusion is a type mrbind can't express, not an API choice.
-- --ignore drops the entity itself; --skip-mentions-of also drops any function naming it.
function api_parser_flags()
    return {
        -- Only feather:: is bound, plus StaticString's namespace below.
        "--ignore", "::",
        "--allow", "feather",
        -- StaticString (core/framework/static_string.hpp) is real Feather infrastructure, just namespaced "nassimp" not "feather".
        -- Needs its own --allow or mrbind refuses to bind anything mentioning it.
        "--allow", "nassimp",

        -- Makes the parse platform-neutral: unparsed, size_t is recorded as unsigned long from a Linux parse, mismatching Windows's unsigned long long downstream (caught cross-compiling to mingw, where RID::id broke this way).
        -- Canonicalizing to uint64_t is correct on every platform that reads the published API description.
        "--canonicalize-to-fixed-size-typedefs",

        -- Internal plumbing under core/: the shared body of the two extension loaders.
        -- Not part of a plugin's API, and unexported -- cannot be linked from outside the engine at all.
        "--ignore", "feather::resolve_and_register_extension_entry",
        -- A scripted component's resolved layout (core/world/scripted_component.h) has std::function accessor fields, with no C/C# spelling.
        -- A binding describes a component and lets the engine lay it out instead.
        "--ignore", "feather::ScriptedFieldLayout",
        "--skip-mentions-of", "feather::ScriptedFieldLayout",
        "--ignore", "feather::ScriptedComponentLayout",
        "--skip-mentions-of", "feather::ScriptedComponentLayout",
        -- --skip-mentions-of does not reach a function that only names the type through a pointer return, so this also needs its own --ignore.
        "--ignore", "feather::find_scripted_component",
        -- Same for what a scripted system's callback is handed (core/world/scripted_system.h) -- raw storage plus reflection info.
        -- Meant for the engine's own language hosts, not a plugin surface.
        "--ignore", "feather::ScriptedSystemComponent",
        "--skip-mentions-of", "feather::ScriptedSystemComponent",
        "--ignore", "feather::ScriptedSystemInvocation",
        "--skip-mentions-of", "feather::ScriptedSystemInvocation",
        "--ignore", "feather::register_scripted_system",
        -- StaticString's operators don't survive the trip to C#: its std::string_view IEquatable instantiates a forbidden Nullable<ReadOnlySpan<char>>, and its two conversion operators collide into a duplicate ToString().
        -- Dropped in favor of the named accessors (str()/data()/size()/hash()), which every language keeps.
        "--ignore", "/nassimp::StaticString::operator.*/",

        -- Standard containers with no C ABI. Matching is by spelling and doesn't cascade -- skipping the element type doesn't skip a container of it.
        -- Each container needs its own regex.
        "--skip-mentions-of", "/std::initializer_list<.*>/",
        "--skip-mentions-of", "/std::span<.*>/",
        "--skip-mentions-of", "/std::array<.*>/",
        "--skip-mentions-of", "/std::tuple<.*>/",
        "--skip-mentions-of", "/std::unordered_map<.*>/",

        -- Third-party types reachable from core's headers. SimpleMath is admitted separately (see c_abi_parser_flags).
        "--skip-mentions-of", "/flecs::.*/",
        "--skip-mentions-of", "/args::.*/",

        -- EngineSettings is a private-constructor singleton holding an unordered_map<string_view, unique_ptr<...>>.
        -- mrbind still attempts a copy-constructor wrapper for the class and fails to compile it.
        "--ignore", "feather::EngineSettings",
        "--skip-mentions-of", "feather::EngineSettings",

        -- Variant type-erases many alternatives (SimpleMath's included): its as<T>()/is<T>() surface needs traits for every alternative.
        -- That holds even when the individual functions are skipped, so it needs --ignore.
        "--ignore", "feather::Variant",
        "--skip-mentions-of", "feather::Variant",
        -- --ignore doesn't cascade into nested types, so VariantArray (built on Variant) needs its own entries, iterators included.
        "--ignore", "feather::VariantArray",
        "--skip-mentions-of", "feather::VariantArray",
        "--ignore", "feather::VariantArray::iterator",
        "--skip-mentions-of", "feather::VariantArray::iterator",
        "--ignore", "feather::VariantArray::const_iterator",
        "--skip-mentions-of", "feather::VariantArray::const_iterator",
        -- ClassInfo carries a Variant-returning factory field -- same cascade.
        "--ignore", "feather::ClassInfo",
        "--skip-mentions-of", "feather::ClassInfo",

        -- Delegate<T>, instantiated exhaustively as bindings do, surfaces a pre-existing bug: class_db.h's subclass_delegate_t fails to compile
        -- delegate.h's forwarding call once actually instantiated.
        "--ignore", "feather::Delegate",
        "--skip-mentions-of", "feather::Delegate",
        -- Same story: StaticIndexedArray<T>::begin() const compares size_t against unsigned long long in std::max (distinct types on Linux).
        -- Broken for any T, but nothing in the engine ever instantiates it.
        "--ignore", "feather::StaticIndexedArray",
        "--skip-mentions-of", "feather::StaticIndexedArray",
        -- consteval: exists only during compilation, so there is no address for a binding to call and no meaning in another language anyway.
        "--ignore", "feather::get_variant_type",
        "--skip-mentions-of", "feather::get_variant_type",

        -- Takes a raw function pointer (the extension entry point), with no binding representation; only the C++ extension loader calls it.
        "--ignore", "feather::register_extension_entry",
        "--skip-mentions-of", "feather::register_extension_entry",

        -- Declared in math_defs.h, never defined -- a dead declaration that only becomes a link error once a generated wrapper force-links it.
        "--ignore", "feather::raise_to_next_multiple_of",
        "--skip-mentions-of", "feather::raise_to_next_multiple_of",
        -- RID::invalid()'s out-of-line definition (core/resources/rid.cpp) is only called from RID::is_valid() in the same TU.
        -- Release-mode GCC constant-folds that call away, so release builds have no symbol.
        "--ignore", "feather::RID::invalid",
        "--skip-mentions-of", "feather::RID::invalid",
    }
end

-- Flags for the parse the C ABI is generated from, admitting the six SimpleMath types core actually uses (core/math/math_defs.h).
-- What gives a C/C++ plugin a Transform with a position.
function c_abi_parser_flags()
    return {
        -- Exposed structs take their fields from their bases, and the parser only copies base members when asked.
        -- Also what lets a derived class offer its inherited methods in C.
        "--copy-inherited-members",

        -- Patterns match whole, so the optional member tail is load-bearing: admitting only the class would reject its own ctors/dtor too.
        -- Only ctors/dtor are admitted, never "::.*": SimpleMath's out-of-line methods (SimpleMath.inl) trip a parser assertion when admitted.
        "--allow", "/DirectX::SimpleMath::(Vector2|Vector3|Vector4|Quaternion|Color|Matrix)"
            .. "(::~?(Vector2|Vector3|Vector4|Quaternion|Color|Matrix))?/",
        -- Their fields live in these bases; a base the parse never saw is dropped from the class entirely, leaving it with no members and no destructor.
        -- XMFLOAT4X4 (Matrix's base) holds an anonymous union the parser won't record as a field -- rules Matrix out of --expose-as-struct only.
        "--allow", "/DirectX::XMFLOAT[234](::.*)?/",
        -- No "(::.*)?" tail here: its members live in the anonymous union, and admitting those crashes the parser (no entity for it to attach to).
        -- Matrix is opaque, so only its own members matter.
        "--allow", "DirectX::XMFLOAT4X4",

        -- The SIMD types the math methods take/return -- no C ABI can pass an __m128, so most of SimpleMath's arithmetic drops out here
        -- (the wrapper aliases these to the plugin's own SimpleMath copy instead). Also catches the matrix/packed shapes from the rejected out-of-line ctors above.
        "--skip-mentions-of", "/DirectX::(XMVECTOR[A-Z0-9]*|XMMATRIX|[FGHC]XMVECTOR|[FC]XMMATRIX|XMFLOAT[34]X[34]|XM(U?INT)[234])/",
        -- XMVECTOR is a compiler vector typedef; matching is by canonical spelling, so the pattern above never catches it off MSVC.
        "--skip-mentions-of", "/.*__vector_size__.*/",
        "--skip-mentions-of", "/DirectX::PackedVector::.*/",

        -- The comparison categories a defaulted operator<=> returns -- no C spelling, and DirectXMath's structs default their comparisons.
        "--skip-mentions-of", "/std::(partial|weak|strong)_ordering/",

        -- Members are deliberately NOT ignored: mrbind decides constructibility/copyability from the parsed constructors.
        -- An opaque Matrix needs those to be constructible and returnable.
    }
end

-- Parses the combined header into `opts.output` as JSON, the API dump every
-- generator reads. Returns the output path.
function run_parse(target, opts)
    opts = opts or {}
    local combined_header = assert(opts.combined_header, "run_parse: opts.combined_header required")
    local output = assert(opts.output, "run_parse: opts.output required")

    local argv = {combined_header, "-o", output, "--format=json"}
    for _, f in ipairs(api_parser_flags()) do
        table.insert(argv, f)
    end
    for _, f in ipairs(opts.extra_parser_flags or {}) do
        table.insert(argv, f)
    end

    table.insert(argv, "--")
    table.insert(argv, "-xc++-header")
    -- Lets a header hide a declaration from the bindings via FEATHER_NO_BIND (export_defs.h) while the compiler still sees it.
    -- --ignore can't reach a static data member, which is what forced the attribute to exist.
    table.insert(argv, "-DFEATHER_MRBIND_PARSE=1")
    table.insert(argv, "-resource-dir=" .. resolve_clang_resource_dir(target))
    for _, f in ipairs(sanitize_compflags_for_mrbind(resolve_compile_flags(target))) do
        table.insert(argv, f)
    end

    os.mkdir(path.directory(output))
    cprint("${cyan}[bindings]${reset} mrbind -> %s", path.relative(output, os.projectdir()))
    os.vrunv(mrbind_bin(target, "mrbind"), argv)
    return output
end

-- Where mrbind_gen_c writes its machine-readable description of the C API.
-- The C# backend is built on top of the C one and reads this rather than api.json (see modules/cs_bindings/xmake.lua).
function c_desc_json_path()
    return path.join(output_dir("c"), "desc.json")
end

-- The mrbind revision thirdparty/packages/mrbind.lua pins, duplicated as a plain string since a package's URL spec isn't reachable from here.
-- The export task asserts the two stay equal.
function mrbind_pinned_commit()
    return "232ff33159d5e76e57b11669453d7d25ad22a14d"
end

-- Paths inside api.json and the exported metadata are always spelled with forward slashes.
-- Every flag derived from them must be too, or Windows backslashes stop matching.
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

-- Identifies the ABI-shaping flags run_gen_c() passes, so a plugin can tell its own flags still match (published as gen_c_flags_id; FeatherPluginSDK.lua refuses to build on a mismatch).
-- Shape only, never paths -- feather_root's absolute path made the hash host-separator-dependent. KEEP IN SYNC with run_gen_c() and FeatherPluginSDK's feather_plugin_bindings.lua.
function gen_c_flags_id()
    local shape = {
        "helper-name-prefix=Feather_",
        "helper-macro-name-prefix=FEATHER_C_",
        "map-path=<root>/core->feather_c",
        "map-path=<root>->feather_c/_root",
        "assume-include-dir=<root>",
        "force-emit-common-helpers",
        "helper-header-dir=feather_helpers",
        -- Placeholder, like the <root> entries above: only the mapping's shape must agree between engine and plugin, never the absolute path.
        "map-path=<directxmath>->feather_c/_ext/directxmath",
        "assume-include-dir=<directxmath>",
    }
    for _, t in ipairs(exposed_struct_types()) do
        table.insert(shape, "expose-as-struct=" .. t)
    end
    return hash.strhash128(table.concat(shape, "\0"))
end

-- The math types a C++ plugin defines itself (same vendored SimpleMath sources), so the generator aliases these instead of wrapping them.
-- Matrix is here too though not an exposed struct -- it crosses as a pointer to a copy. KEEP IN SYNC with the SDK's feather_plugin_bindings.lua.
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

-- The C++ types emitted as real C structs (cross the ABI by value) rather than opaque pointers -- only the union-free SimpleMath types qualify.
-- KEEP IN SYNC with the SDK's shape_flags() and gen_c_argv() in tools/SDK/modules/feather_plugin_bindings.lua.
function exposed_struct_types()
    return {
        "DirectX::SimpleMath::Vector2",
        "DirectX::SimpleMath::Vector3",
        "DirectX::SimpleMath::Vector4",
        "DirectX::SimpleMath::Quaternion",
        "DirectX::SimpleMath::Color",
    }
end

-- mrbind_gen_c's exception-relaying helper calls typeid() without including <typeinfo> -- only surfaces under clang-cl, where <exception> doesn't drag it in.
-- Patches the generator's own output rather than force-including into the whole target, since the bug is specific to this one generated file.
function _fixup_missing_typeinfo_include(source_dir)
    local details_cpp = path.join(source_dir, "__mrbind_c_details.cpp")
    if not os.isfile(details_cpp) then
        return
    end
    local content = io.readfile(details_cpp)
    if content:find("#include <typeinfo>", 1, true) then
        return
    end
    -- Prefer anchoring next to <exception>, the header whose absence this is actually working around; fall back to right after the file's own
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

-- Content-compare copy of a generated tree: an unchanged file keeps its mtime, so identical output doesn't force a rebuild of every consumer.
-- Files that vanished from `src` are removed from `dst`.
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
        -- The generator is documented to emit .cpp implementation files; fall back to .c in case a given version differs.
        sources = os.files(path.join(source_dir, "**.c"))
    end
    return sources
end

-- A generator only needs to re-run when its input or flags change. A stamp holding both, written on success, lets an unchanged rebuild skip it
-- entirely -- more robust than mtime, which a `touch` would defeat.
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

    -- mrbind_gen_c rewrites every output file on every run, so it generates into a staging tree first, and _sync_tree copies across only what changed.
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
        -- NOT "FEATHER_": that would make the generated exports.h define FEATHER_API, colliding with export_defs.h's own former definition.
        "--helper-macro-name-prefix", opts.helper_macro_prefix or "FEATHER_C_",
        -- core/main/init_level.h becomes <feather_c/main/init_level.h>. Distinct from --assume-include-dir's ORIGINAL-header spelling below,
        -- or a generated .cpp's quoted/angled includes would resolve identically, leaving resolution to -I order alone.
        "--map-path", to_forward_slashes(feather_root) .. "/core", "feather_c",
        "--map-path", to_forward_slashes(feather_root), "feather_c/_root",
        -- Parsed from outside the engine tree; see directxmath_includedir.
        "--map-path", to_forward_slashes(directxmath_includedir(target)), "feather_c/_ext/directxmath",
        "--assume-include-dir", to_forward_slashes(feather_root),
        -- The glue includes the real <DirectXMath.h> to call into it, distinct from the mapping above (which spells the generated header).
        "--assume-include-dir", to_forward_slashes(directxmath_includedir(target)),
        "--clean-output-dirs",
        "--output-desc-json", stage_desc,
        -- The C# bindings need this header regardless of whether the C bindings alone would have pulled it in, so it's always emitted.
        "--force-emit-common-helpers",
        -- Keeps generated helper headers out of the top level of the include dir, where they'd sit among the mirrored engine headers.
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
    -- desc.json feeds the C# generator; replaced only when changed, so an unchanged C parse doesn't cascade into a C# regeneration.
    _sync_file(stage_desc, c_desc_json_path())
    os.tryrm(stage)

    io.writefile(gen_c_stamp_path(), _gen_stamp_value(api_json_path(), gen_c_flags_id()))
    return _list_gen_c_sources(source_dir)
end

-- The flags feather_gen_cpp needs beyond its input and output paths, shared so the engine's own generation and a plugin's cannot disagree.
-- KEEP IN SYNC with the SDK's gen_cpp_argv().
function gen_cpp_shape_flags()
    local argv = {}
    for _, t in ipairs(native_math_types()) do
        table.insert(argv, "--native-type")
        table.insert(argv, t)
        table.insert(argv, "SimpleMath.h")
    end
    -- The engine spells these unqualified in its own headers (core/math/math_defs.h); a plugin gets the same spellings.
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
    -- Unlike mrbind's generators, this one is ours and changes with the engine, so its own binary hash must feed the stamp too.
    local generator = mrbind_bin(target, "feather_gen_cpp")

    -- The generator ships inside a cached package: editing its sources changes the requested config without necessarily reinstalling, so
    -- compare content (not mtime -- a fresh checkout rewrites every mtime) against what the installed package was actually built from.
    if opts.gen_cpp_dir and os.isdir(opts.gen_cpp_dir) then
        local manifest = path.join(path.directory(path.directory(generator)), "manifest.txt")
        local recorded = os.isfile(manifest) and io.readfile(manifest):match('gen_cpp_rev = "([^"]*)"')
        local current = feather_gen_cpp_rev(opts.gen_cpp_dir)
        if recorded and recorded ~= "" and current ~= "" and recorded ~= current then
            raise("feather_gen_cpp was built from different sources than tools/SDK/gen_cpp holds now.\n"
                .. "  It lives in the mrbind package, whose install xmake has cached, so editing it\n"
                .. "  does not reinstall on its own. Rebuild with:  xmake f -c -y")
        end
    end

    local flags_id = hash.strhash128(table.concat(flags, "\0") .. "\0" .. hash.sha256(generator))

    local stamp = output_dir .. ".stamp"
    if _gen_outputs_fresh(stamp, desc_json, flags_id,
            #os.files(path.join(output_dir, "**.hpp")) > 0) then
        return output_dir
    end

    -- Staged and content-synced for the same reason as the other generators: an unchanged regeneration must not bump the emitted headers' mtimes.
    local stage = output_dir .. ".stage"
    os.tryrm(stage)
    os.mkdir(stage)

    local argv = {"--input-json", desc_json, "--output-dir", stage, "--clean-output-dir"}
    for _, f in ipairs(flags) do
        table.insert(argv, f)
    end

    cprint("${cyan}[cpp_bindings]${reset} feather_gen_cpp -> %s", path.relative(output_dir, os.projectdir()))
    os.vrunv(generator, argv)

    -- The hand-written half of the surface -- the entry point macro and the ECS registration API -- describes a plugin, so it isn't generated.
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

    -- Skip the generator when its input (the C descriptor) is byte-for-byte what produced the current output.
    -- The lib name is the only caller-varied flag that reaches the output.
    local csharp_flags_id = opts.imported_lib_name or "feather_c"
    local stamp = output_dir .. ".stamp"
    if _gen_outputs_fresh(stamp, desc_json, csharp_flags_id,
            #os.files(path.join(output_dir, "**.cs")) > 0) then
        return output_dir
    end

    -- Staged and content-synced like the C generator; a sibling of output_dir, not a child, so the sync walk never sees it.
    local stage = output_dir .. ".stage"
    os.tryrm(stage)
    os.mkdir(stage)

    local argv = {
        "--input-json", desc_json,
        "--output-dir", stage,
        -- The name the generated [DllImport]s carry. It names no file on disk: the C bindings are compiled into the engine executable, so a
        -- plugin's DllImportResolver maps this onto the running process.
        "--imported-lib-name", opts.imported_lib_name or "feather_c",
        -- C# has no free functions, so the generator puts helpers in a static class; the C++-style spelling here becomes Feather.Misc.* in C#.
        "--helpers-namespace", "Feather::Misc",
        -- No --force-namespace: the C++ side already lives in `feather`, which the generator maps to `Feather` on its own -- forcing it too
        -- produced `Feather.Feather.X`, and non-compiling IEquatable output.
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
