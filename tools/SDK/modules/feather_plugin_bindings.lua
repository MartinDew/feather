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
-- Hashes only the *shape* of the flags, never the paths.
--
-- feather_root is data from the metadata and identical on both sides, so
-- hashing it detects nothing -- and hashing it through path.join() made the
-- result depend on the host separator, so a Windows plugin build disagreed with
-- a Linux-exported file and reported drift that did not exist.
--
-- KEEP IN SYNC with the engine's gen_c_flags_id() in
-- xmake/modules/feather_bindings.lua.
local function shape_flags()
    return {
        "helper-name-prefix=Feather_",
        "helper-macro-name-prefix=FEATHER_C_",
        "map-path=<root>/core->feather_c",
        "map-path=<root>->feather_c/_root",
        "assume-include-dir=<root>",
        "force-emit-common-helpers",
        "helper-header-dir=feather_helpers",
    }
end

function gen_c_flags_id()
    return hash.strhash128(table.concat(shape_flags(), "\0"))
end

-- Paths in api.json and the metadata are spelled with forward slashes; keep
-- every flag derived from them that way, or Windows translates them to
-- backslashes and the prefix stops matching what the parse recorded.
local function to_forward_slashes(p)
    return (tostring(p):gsub("\\", "/"))
end

local function check_flags_id(meta, api_meta_name)
    if not meta.gen_c_flags_id then
        -- Older export, or one written by hand. Nothing to compare against.
        return
    end
    local ours = gen_c_flags_id()
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
        -- Concatenated, not path.join()'d: see to_forward_slashes above. The
        -- engine's run_gen_c() derives these exactly the same way.
        "--map-path", to_forward_slashes(feather_root) .. "/core", "feather_c",
        "--map-path", to_forward_slashes(feather_root), "feather_c/_root",
        "--assume-include-dir", to_forward_slashes(feather_root),
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

-- Wires up linking for a Windows plugin.
--
-- ELF and Mach-O let a plugin leave its feather_* imports undefined and bind
-- them when the engine dlopens it. Windows has no equivalent, so the plugin has
-- to link feather_c's import library at build time. `xmake export-api`
-- publishes that library next to feather_api.json for exactly this, so a plugin
-- project still vendors one directory and never names an engine build tree.
function apply_windows_link(target, opts)
    if not is_plat("windows", "mingw") then
        return
    end

    local dirs = {}
    if opts.api_json then
        table.insert(dirs, path.directory(path.absolute(opts.api_json, os.projectdir())))
    end
    if opts.feather_c_libdir then
        table.insert(dirs, path.absolute(opts.feather_c_libdir, os.projectdir()))
    end

    for _, dir in ipairs(dirs) do
        for _, pattern in ipairs({"*feather_c*.lib", "*feather_c*.dll.a"}) do
            if #os.files(path.join(dir, pattern)) > 0 then
                target:add("linkdirs", dir)
                target:add("links", "feather_c")
                return
            end
        end
    end

    raise("FeatherPluginSDK: no feather_c import library found for this Windows build.\n"
        .. "  Looked in: " .. table.concat(dirs, ", ") .. "\n"
        .. "  Publish one from the engine with `xmake export-api` and copy it next to\n"
        .. "  feather_api.json, or point opts.feather_c_libdir at the engine's build/bin.")
end

-- The .NET Runtime Identifier for the machine actually running dotnet.
--
-- NativeAOT cannot cross the OS boundary at all -- "Cross-OS native
-- compilation is not supported" is ILCompiler's own message for it -- so the
-- one RID that can always be published here is the host's own, regardless of
-- what platform xmake itself is configured for. A hardcoded "linux-x64"
-- default broke every non-Linux machine outright: dotnet ran, picked up the
-- forced RID, and refused before compiling anything.
--
-- os.host()/os.arch() name the machine dotnet is actually running on; xmake's
-- is_plat()/is_arch() name the *target* xmake was configured for, which is the
-- wrong question for a tool that cannot cross-compile regardless of what the
-- rest of the build is doing.
local function host_dotnet_rid()
    local os_part = ({windows = "win", linux = "linux", macosx = "osx"})[os.host()]
    local arch_part = ({x86_64 = "x64", x64 = "x64", i386 = "x86", arm64 = "arm64", ["arm64-v8a"] = "arm64"})[os.arch()]
    if not os_part or not arch_part then
        raise("FeatherPluginSDK: don't know the .NET RID for " .. os.host() .. "/" .. os.arch()
            .. " -- pass opts.runtime explicitly (e.g. \"win-x64\", \"linux-x64\", \"osx-arm64\").")
    end
    return os_part .. "-" .. arch_part
end

-- The filename dotnet's NativeAOT publish actually produces: the assembly
-- name (the csproj's own filename, absent an explicit <AssemblyName>) plus
-- whatever shared-library extension is native to the host -- .dll here,
-- .so/.dylib elsewhere, never .so unconditionally the way a hardcoded default
-- would assume.
local function default_published_name(csproj)
    local ext = ({windows = ".dll", linux = ".so", macosx = ".dylib"})[os.host()] or ".so"
    return path.basename(csproj) .. ext
end

-- The filename staged into bin/ -- and so the one a .fext manifest's
-- "libraries" table names. Mirrors feather_c_plugin's own convention (no "lib"
-- prefix on Windows, via set_prefixname(""); the platform default elsewhere),
-- so a C and a C# extension of the same logical name are found the same way.
local function default_output_name(name)
    if os.host() == "windows" then
        return name .. ".dll"
    elseif os.host() == "macosx" then
        return "lib" .. name .. ".dylib"
    else
        return "lib" .. name .. ".so"
    end
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
        "-r", opts.runtime or host_dotnet_rid(),
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

    local published_name = opts.published_name or default_published_name(csproj)
    local produced = path.join(stage, "publish", published_name)
    assert(os.isfile(produced), "FeatherPluginSDK: dotnet publish produced no " .. produced
        .. "\n  (looked for the assembly name derived from " .. path.filename(csproj)
        .. "; pass opts.published_name if <AssemblyName> overrides it in the .csproj)")

    local output_name = opts.output_name or default_output_name(target:name())
    local bindir = path.join(os.projectdir(), "bin")
    os.mkdir(bindir)
    os.vcp(produced, path.join(bindir, output_name))
    cprint("${cyan}[feather]${reset} -> bin/%s", output_name)
end
