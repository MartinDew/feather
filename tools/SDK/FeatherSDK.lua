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
includes(path.join(FEATHER_ROOT, "thirdparty", "xmake.lua"))
includes(path.join(FEATHER_ROOT, "xmake", "public_api.lua"))

-- variant: "editor" or "standalone" -- must match the engine binary this DLL loads into.
-- opts.codegen_dirs: dirs to run reflection codegen over (default {"src"}); pass {} to opt out.
-- opts.codegen_extensions: project modifier extensions, scoped to codegen_dirs.
-- opts.engine_bin_dir: defaults to <engine>/build/bin.
function feather_sdk_setup(target_name, variant, opts)
    opts = opts or {}
    variant = variant or "standalone"
    if variant ~= "editor" and variant ~= "standalone" then
        -- assert() is unavailable at description scope, so warn instead of hard-failing.
        print("[feather] feather_sdk_setup: variant must be 'editor' or 'standalone', got: " .. tostring(variant) .. " -- defaulting to 'standalone'")
        variant = "standalone"
    end

    local bin_dir = opts.engine_bin_dir or path.join(FEATHER_ROOT, "build", "bin")
    local engine_bin = path.join(bin_dir, "feather." .. variant)

    local codegen_dirs = opts.codegen_dirs or {"src"}
    local codegen_extensions = opts.codegen_extensions

    target(target_name)
        add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
        add_deps("feather_public_api")

        -- before_build/on_load closures run sandboxed and can't see this
        -- function's locals or resolve os.scriptdir() to this file across a
        -- cross-repo includes(), so hand the engine root over as a target value.
        set_values("feather.root", FEATHER_ROOT)

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
            assert(os.isfile(bin) or (is_plat("windows") and os.isfile(bin .. ".exe")),
                "[feather] engine binary not found: " .. bin ..
                "\n[feather] Build FeatherEngine first (cd " .. feather_root .. " && xmake)," ..
                "\n[feather] or point feather_sdk_setup's engine_bin_dir at your engine build dir.")
        end)

        -- Same flags the engine compiles itself with (needed for FCLASS/FSTRUCT attributes).
        on_config(function(target)
            import("feather_flags")
            feather_flags.apply(target)
        end)

        if is_plat("windows") then
            add_linkdirs(bin_dir)
            add_links("feather." .. variant)
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
