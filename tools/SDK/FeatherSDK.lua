-- FeatherSDK.lua: replaces tools/generate_export.cmake. Usage: see
-- tools/templates/consumer_xmake_template.lua. Public API surface comes
-- entirely from feather_public_api via add_deps(), so a new engine
-- dependency needs no edits here.
local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))

-- Lets a consumer import() feather_codegen/feather_flags from its own project.
add_moduledirs(path.join(FEATHER_ROOT, "xmake", "modules"))

-- Matches root xmake.lua's include order so package configs hash-match the
-- engine's own xrepo cache instead of building a second variant.
includes(path.join(FEATHER_ROOT, "xmake", "options.lua"))

-- Must mirror root xmake.lua's MSVC runtime choice, project-wide and before
-- the includes() below -- per-target here left simplemath's own target
-- (declared inside thirdparty/xmake.lua) on the default runtime, LNK2038.
if has_config("production") or has_config("static_cpp") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
end

includes(path.join(FEATHER_ROOT, "thirdparty", "xmake.lua"))
includes(path.join(FEATHER_ROOT, "xmake", "public_api.lua"))

-- This DLL must be built in the same xmake configure as the engine binary
-- it's loading into, so EDITOR_BUILD (from feather_public_api via add_deps
-- below) matches the already-built feather binary's value -- see
-- xmake/public_api.lua's comment on why that has to be one shared define.
--
-- opts.codegen_dirs: dirs to run reflection codegen over (default {"src"}); pass {} to opt out.
-- opts.codegen_extensions: project modifier extensions, scoped to codegen_dirs.
-- opts.engine_bin_dir: defaults to <engine>/build/bin.
function feather_sdk_setup(target_name, opts)
    opts = opts or {}

    local bin_dir = opts.engine_bin_dir or path.join(FEATHER_ROOT, "build", "bin")
    local engine_bin = path.join(bin_dir, "feather")

    local codegen_dirs = opts.codegen_dirs or {"src"}
    local codegen_extensions = opts.codegen_extensions

    target(target_name)
        add_deps("feather_public_api")

        -- before_build/on_load closures run sandboxed and can't see this
        -- function's locals or resolve os.scriptdir() to this file across a
        -- cross-repo includes(), so hand the engine root over as a target value.
        set_values("feather.root", FEATHER_ROOT)

        -- Belt and suspenders with the file-scope set_runtimes() above: that
        -- alone fixed thirdparty/SimpleMath's target but left THIS target on
        -- the default runtime (confirmed the LNK2038 direction flipped) --
        -- unclear why given both are plain description-scope calls, but this
        -- target is also reopened later (consumer's own xmake.lua sets its
        -- kind there), so setting it again here, closest to declaration, is
        -- the safest bet without a real MSVC toolchain to verify against.
        if has_config("production") or has_config("static_cpp") then
            set_runtimes(is_mode("debug") and "MTd" or "MT")
        end

        -- Must mirror root xmake.lua's per-mode defines, or engine headers
        -- compiled into both the exe and this DLL disagree on #if BETA/PRODUCTION.
        if is_mode("debug", "releasedbg") then
            add_defines("BETA")
        end
        if is_mode("release") then
            add_defines("PRODUCTION")
        end

        for _, entry in ipairs(codegen_dirs) do
            local d = type(entry) == "table" and entry.dir or entry
            local name = type(entry) == "table" and entry.name or path.basename(d)
            add_includedirs(d)
            -- always_added: file doesn't exist until before_build below has run once.
            add_files(path.join(d, "register_" .. name .. "_types.gen.cpp"), {always_added = true})
        end

        if #codegen_dirs > 0 then
            for _, entry in ipairs(codegen_dirs) do
                local d = type(entry) == "table" and entry.dir or entry
                local name = type(entry) == "table" and entry.name or path.basename(d)
                add_values("feather.codegen_dirs", name .. "=" .. d)
            end
            for _, ext in ipairs(codegen_extensions or {}) do
                add_values("feather.codegen_extensions", ext)
            end

            before_build(function(target)
                import("feather_codegen")
                feather_codegen.run_project_codegen(
                    table.wrap(target:values("feather.root"))[1],
                    os.projectdir(),
                    table.wrap(target:values("feather.codegen_dirs")),
                    {extensions = table.wrap(target:values("feather.codegen_extensions"))})
            end)
        end

        -- Consumers link against the engine exe and rely on core's .gen.h
        -- already existing, so fail early and readably if it isn't built yet.
        set_values("feather.engine_bin", engine_bin)
        on_load(function(target)
            local bin = table.wrap(target:values("feather.engine_bin"))[1]
            local feather_root = table.wrap(target:values("feather.root"))[1]
            assert(os.isfile(bin) or (is_plat("windows", "mingw") and os.isfile(bin .. ".exe")),
                "[feather] engine binary not found: " .. bin ..
                "\n[feather] Build FeatherEngine first (cd " .. feather_root .. " && xmake)," ..
                "\n[feather] or point feather_sdk_setup's engine_bin_dir at your engine build dir.")
        end)

        -- Same flags the engine compiles itself with (needed for FCLASS/FSTRUCT attributes).
        on_config(function(target)
            import("feather_flags")
            feather_flags.apply(target)
        end)

        if is_plat("windows", "mingw") then
            -- mingw is a distinct is_plat() from "windows" but needs this
            -- same link-time path, not the else branch's ELF dlopen() binding.
            add_linkdirs(bin_dir)
            add_links("feather")

            if is_plat("mingw") then
                -- mingw's libstdc++/libgcc aren't a Windows system component, so
                -- static-link like the exe does; add_shflags (add_ldflags no-ops on shared kind).
                add_shflags("-static-libgcc", "-static-libstdc++",
                    "-Wl,-Bstatic,--whole-archive", "-lwinpthread", "-Wl,--no-whole-archive,-Bdynamic",
                    {force = true})
                -- Same as xmake/engine.lua: mingw-w64's libstdc++ splits std::print's
                -- terminal-writing internals into a separate libstdc++exp.a archive.
                add_syslinks("stdc++exp")
            end
        else
            -- No link target on ELF/Mach-O: undefined engine/flecs/SDL symbols
            -- bind to the host exe at dlopen() time (-rdynamic, {links = {}}).
            if is_plat("macosx") then
                -- Mach-O rejects undefined symbols in a dylib by default. Untested.
                add_shflags("-undefined", "dynamic_lookup", {force = true})
            end
        end
    target_end()
end
