-- Binding generation for Feather plugin projects.
--
-- An importable module rather than plain functions in FeatherPluginSDK.lua:
-- functions defined in an includes()'d description file keep the description
-- sandbox as their lexical environment, which has no assert(), no import() and
-- no io -- even when called from on_config(). A module runs in the script
-- sandbox and has all of it.
--
-- Everything here turns the engine's published feather_api.json into the
-- headers and sources a plugin compiles. mrbind's generators are pure
-- JSON-to-text tools -- no Clang, no LLVM, no engine checkout -- so this is the
-- entire toolchain a C or C# plugin needs.

import("core.base.json")

-- Where generated bindings land: under build/ so it's disposable, and out of
-- the source tree so the engine's project walk never sees it.
function bindings_dir()
    return path.join(os.projectdir(), "build", "feather_bindings")
end

function output_layout()
    local root = bindings_dir()
    return {
        header_dir = path.join(root, "include"),
        -- Generated but never compiled: the engine ships this code already
        -- built, inside libfeather_c. mrbind has no way to skip emitting it.
        source_dir = path.join(root, "unused-glue"),
        desc_json  = path.join(root, "desc.json"),
        csharp_dir = path.join(root, "csharp"),
    }
end

function generator_bin(target, name)
    local pkg = assert(target:pkg("mrbind_generators"),
        "FeatherPluginSDK: target must add_packages(\"mrbind_generators\")")
    local suffix = is_plat("windows") and ".exe" or ""
    return path.join(pkg:installdir(), "bin", name .. suffix)
end

-- Reads the sidecar written next to feather_api.json by the engine's
-- `export-api` task. It carries the few things the generators need that the API
-- file doesn't state -- above all feather_root, the engine checkout path baked
-- into every filename inside api.json, which --map-path and --assume-include-dir
-- must be given verbatim to match. That path does not have to exist on this
-- machine; nothing ever opens it.
function read_api_meta(api_json, api_meta)
    api_meta = api_meta or path.join(path.directory(api_json),
        path.basename(api_json) .. ".meta.json")
    assert(os.isfile(api_meta), "FeatherPluginSDK: missing API metadata file: " .. api_meta
        .. "\n  It is published alongside feather_api.json by the engine's `xmake export-api` task.")

    local meta = json.loadfile(api_meta)
    assert(meta and meta.feather_root,
        "FeatherPluginSDK: " .. api_meta .. " has no \"feather_root\"")
    return meta, api_meta
end

-- The ABI-shaping flags, identified so drift against the engine is caught at
-- build time rather than as a link error or, worse, silent UB.
--
-- KEEP IN SYNC with the engine's run_gen_c() and gen_c_flags_id() in
-- xmake/modules/feather_bindings.lua.
local function shaping_flags(feather_root)
    return {
        "Feather_", "FEATHER_C_",
        path.join(feather_root, "core"), "feather_c",
        feather_root, "feather_c/_root",
        feather_root,
        "--force-emit-common-helpers",
        "feather_helpers",
    }
end

function gen_c_flags_id(feather_root)
    return hash.strhash128(table.concat(shaping_flags(feather_root), "\0"))
end

local function check_flags_id(meta, api_meta_name)
    if not meta.gen_c_flags_id then
        -- Older export, or one written by hand. Nothing to compare against.
        return
    end
    local ours = gen_c_flags_id(meta.feather_root)
    assert(ours == meta.gen_c_flags_id, string.format(
        "FeatherPluginSDK: binding flags disagree with the engine that produced this API file.\n"
        .. "  engine (%s): %s\n  this SDK:     %s\n"
        .. "  The generated headers would not match the shipped libfeather_c.\n"
        .. "  Update the vendored SDK to the one from that engine build.",
        api_meta_name, meta.gen_c_flags_id, ours))
end

-- Every shaping flag here must match the engine's run_gen_c() exactly: the
-- headers generated here describe an ABI that libfeather_c already implements.
local function gen_c_argv(api_json, feather_root, out)
    return {
        "--input", api_json,
        "--output-header-dir", out.header_dir,
        "--output-source-dir", out.source_dir,
        "--helper-name-prefix", "Feather_",
        "--helper-macro-name-prefix", "FEATHER_C_",
        -- Consumers include through this prefix: <feather_c/math/projection.h>.
        "--map-path", path.join(feather_root, "core"), "feather_c",
        "--map-path", feather_root, "feather_c/_root",
        "--assume-include-dir", feather_root,
        "--clean-output-dirs",
        "--output-desc-json", out.desc_json,
        "--force-emit-common-helpers",
        "--helper-header-dir", "feather_helpers",
    }
end

-- Regenerating takes a few seconds over a multi-megabyte JSON file, so skip it
-- when nothing that feeds it has changed.
local function outputs_are_stale(api_json, out)
    if not os.isfile(out.desc_json) or not os.isdir(out.header_dir) then
        return true
    end
    return os.mtime(api_json) > os.mtime(out.desc_json)
end

-- Generates the C headers, and with want_csharp the C# sources, that a plugin
-- compiles against. Returns the output layout.
function generate(target, opts, want_csharp)
    local api_json = assert(opts.api_json, "FeatherPluginSDK: opts.api_json is required")
    api_json = path.absolute(api_json, os.projectdir())
    assert(os.isfile(api_json), "FeatherPluginSDK: API file not found: " .. api_json
        .. "\n  Copy it from the engine's build/bindings/dist/ (see `xmake export-api`).")

    local out = output_layout()
    local meta, meta_path = read_api_meta(api_json, opts.api_meta)
    check_flags_id(meta, meta_path)

    if outputs_are_stale(api_json, out) then
        os.mkdir(out.header_dir)
        os.mkdir(out.source_dir)
        cprint("${cyan}[feather]${reset} mrbind_gen_c -> %s",
            path.relative(out.header_dir, os.projectdir()))
        os.vrunv(generator_bin(target, "mrbind_gen_c"), gen_c_argv(api_json, meta.feather_root, out))
    end

    if want_csharp then
        os.mkdir(out.csharp_dir)
        cprint("${cyan}[feather]${reset} mrbind_gen_csharp -> %s",
            path.relative(out.csharp_dir, os.projectdir()))
        os.vrunv(generator_bin(target, "mrbind_gen_csharp"), {
            "--input-json", out.desc_json,
            "--output-dir", out.csharp_dir,
            -- The library the engine preloads; P/Invoke finds it by this name.
            "--imported-lib-name", "feather_c",
            "--helpers-namespace", "Feather::Misc",
            -- No --force-namespace: the C++ `feather` namespace already maps
            -- to `Feather`. Forcing it too yields `Feather.Feather.X` and does
            -- not compile. KEEP IN SYNC with the engine's run_gen_csharp().
            -- Required once the directory exists; the generator refuses to
            -- write into a non-empty output dir otherwise.
            "--clean-output-dir",
        })
    end

    return out
end

-- Publishes a C# plugin with NativeAOT, so the result is an ordinary native
-- shared library the engine loads exactly like a C one.
function publish_csharp(target, opts, out)
    import("lib.detect.find_tool")

    local dotnet = assert(find_tool("dotnet"),
        "FeatherPluginSDK: dotnet SDK not found on PATH (needed for C# extensions)")
    local csproj = path.absolute(assert(opts.csproj, "FeatherPluginSDK: opts.csproj is required"),
        os.projectdir())

    -- Staged outside the project: the engine's project walk opens every shared
    -- library it finds, and dotnet's intermediate directories are full of them.
    local stage = path.join(os.tmpdir(), "feather_cs_plugin", target:name())
    os.tryrm(stage)
    os.mkdir(stage)

    cprint("${cyan}[feather]${reset} dotnet publish %s", path.filename(csproj))
    os.vrunv(dotnet.program, {
        "publish", csproj,
        "-c", "Release",
        "-r", opts.runtime or "linux-x64",
        -- Emits a plain native .so exporting the [UnmanagedCallersOnly] entry
        -- points, rather than a managed assembly needing a host.
        "-p:NativeLib=Shared",
        "-p:PublishAot=true",
        -- Where the generated .cs files are; the csproj globs this.
        "-p:FeatherCsharpDir=" .. out.csharp_dir,
        "-p:BaseIntermediateOutputPath=" .. path.join(stage, "obj") .. "/",
        "-p:BaseOutputPath=" .. path.join(stage, "out") .. "/",
        "-o", path.join(stage, "publish"),
    }, {envs = {DOTNET_CLI_TELEMETRY_OPTOUT = "1", DOTNET_NOLOGO = "1"}})

    local produced = path.join(stage, "publish", opts.published_name or "CsExample.so")
    assert(os.isfile(produced), "FeatherPluginSDK: dotnet publish produced no " .. produced)

    local bindir = path.join(os.projectdir(), "bin")
    os.mkdir(bindir)
    os.vcp(produced, path.join(bindir, opts.output_name or ("lib" .. target:name() .. ".so")))
    cprint("${cyan}[feather]${reset} -> bin/%s", opts.output_name or ("lib" .. target:name() .. ".so"))
end
