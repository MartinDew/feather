-- FeatherSDK.lua: replaces tools/generate_export.cmake.
--
-- External consumers ("project DLL" repos loaded at runtime by the feather
-- executable via _load_extension()) locate a FeatherEngine checkout
-- themselves (see tools/templates/consumer_xmake_template.lua for the
-- standard discovery block), then:
--
--   includes("/path/to/feather/tools/SDK/FeatherSDK.lua")
--   feather_sdk_setup("mygame_plugin", {
--       codegen_dirs = { {dir = "src", name = "mygame"} },
--   })
--
--   target("mygame_plugin")                            -- SEPARATE, reopened block --
--       set_kind("shared")                              -- feather_sdk_setup() opens
--       add_files("src/*.cpp")                           -- and closes its own target
--   target_end()                                         -- scope internally, so this
--                                                         -- must come after, not nested
--                                                         -- inside it (mirrors how
--                                                         -- modules/vex_renderer/xmake.lua
--                                                         -- reopens vex_renderer
--                                                         -- after feather_module_target()).
--
-- Public API surface (include dirs + thirdparty packages) comes entirely
-- from feather_public_api (xmake/public_api.lua) via one-hop add_deps() --
-- this file never hand-lists packages/includedirs itself, so a new public
-- thirdparty dependency or an SDK-exposing module needs zero edits here.
local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))

-- Lets a consumer import() engine build modules (feather_codegen, feather_flags)
-- from its own on_config/before_build closures. Without this the consumer's
-- moduledirs only cover its own repo and import() fails outright.
add_moduledirs(path.join(FEATHER_ROOT, "xmake", "modules"))

-- Same include order as root xmake.lua, so package configs (e.g. static_deps)
-- hash-match the engine's own already-built xrepo cache entries instead of
-- xrepo building a second (e.g. shared-lib) variant just for the consumer.
includes(path.join(FEATHER_ROOT, "xmake", "options.lua"))
includes(path.join(FEATHER_ROOT, "thirdparty", "xmake.lua"))
includes(path.join(FEATHER_ROOT, "xmake", "public_api.lua"))

-- Must match root xmake.lua's own set_runtimes() block exactly (same
-- has_config() conditions, same MTd/MDd strings) -- this is a SEPARATE
-- xmake project (the consumer's own xmake.lua includes() this file) that
-- otherwise resolves its own default CRT linkage independently of the
-- engine's. Confirmed in CI: without this, feather.exe defaulted to /MTd
-- (static) while a plugin built through this file defaulted to /MDd
-- (dynamic) -- same mode, same toolchain, disagreeing defaults -- and the
-- resulting CRT mismatch hung the plugin inside LoadLibrary on Windows
-- with no diagnostic output at all. See root xmake.lua's matching comment.
if has_config("production") or has_config("static_cpp") then
    set_runtimes(is_mode("debug") and "MTd" or "MT")
elseif is_plat("windows") then
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

-- feather_sdk_setup(target_name, opts)
--
-- opts (all optional):
--   codegen_dirs        : source dirs to run reflection codegen over, relative
--                         to the consumer's project root. Each entry is either
--                         a path string or {dir = ..., name = ...}, where name
--                         overrides the generated register_<name>_types symbol
--                         (default: the dir's basename -- so a plain "src"
--                         would give register_src_types). Default {"src"}.
--                         Pass {} to opt out of codegen entirely.
--   codegen_extensions  : project-owned modifier extensions (.py files or dirs
--                         of them) passed through to generate_reflection.py --
--                         see tools/codegen/modifier_api.py. Scoped to
--                         codegen_dirs; a project extension can never affect
--                         what core/ generates.
--   engine_bin_dir      : where feather was built. Defaults to
--                         <engine>/build/bin, matching root xmake.lua's
--                         set_targetdir("$(builddir)/bin"); override if the
--                         engine was configured with a custom build dir.
--
-- Back-compat: a caller still passing the old (target_name, variant, opts)
-- form -- "editor"/"standalone" used to select EDITOR_BUILD and which of two
-- engine binaries to link against; there is now only one feather binary and
-- editor mode is a runtime flag (--editor), so the argument does nothing --
-- is accepted for one release with a deprecation notice rather than a hard
-- break. Detected by argument shape: the new form's 2nd argument is a table
-- (or omitted), the old form's was always the "editor"/"standalone" string.
function feather_sdk_setup(target_name, variant_or_opts, maybe_opts)
    local opts
    if type(variant_or_opts) == "string" then
        print("[feather] feather_sdk_setup: the (name, variant, opts) form is deprecated -- " ..
              "there is only one feather binary now and editor mode is the --editor runtime " ..
              "flag (see Engine::is_editor()). Call feather_sdk_setup(name, opts) instead.")
        opts = maybe_opts or {}
    else
        opts = variant_or_opts or {}
    end

    local bin_dir = opts.engine_bin_dir or path.join(FEATHER_ROOT, "build", "bin")
    local engine_bin = path.join(bin_dir, "feather")

    local codegen_dirs = opts.codegen_dirs or {"src"}
    local codegen_extensions = opts.codegen_extensions

    target(target_name)
        add_deps("feather_public_api")

        -- Closures passed to before_build/on_load run in a sandbox that can't
        -- see this function's locals, and neither os.scriptdir() nor
        -- os.projectdir() point at the engine from inside one (both resolve to
        -- the top-level consumer project across a cross-repo includes()), so
        -- the engine root has to be handed over as a target value.
        set_values("feather.root", FEATHER_ROOT)

        -- Mirror root xmake.lua's per-mode defines. Engine headers are compiled
        -- into both the exe and this DLL, so a mismatch here would give the two
        -- different views of any #if BETA / #ifdef PRODUCTION member -- an ODR
        -- violation that only shows up as memory corruption at runtime.
        if is_mode("debug", "releasedbg") then
            add_defines("BETA")
        end
        if is_mode("release") then
            add_defines("PRODUCTION")
        end

        for _, entry in ipairs(codegen_dirs) do
            local d = type(entry) == "table" and entry.dir or entry
            local name = type(entry) == "table" and entry.name or path.basename(d)
            -- generate_reflection.py computes the #includes in
            -- register_<name>_types.gen.cpp relative to the scanned dir itself
            -- (its include_base), so that dir has to be on the include path --
            -- same reason feather_module_target() adds module_dir.
            add_includedirs(d)
            -- always_added: this doesn't exist on disk until the before_build
            -- below has run at least once, and xmake would otherwise fail its
            -- description-time file-existence check. Matches GENERATED_SOURCE in
            -- root xmake.lua and feather_module_target()'s generated_files.
            add_files(path.join(d, "register_" .. name .. "_types.gen.cpp"), {always_added = true})
        end

        if #codegen_dirs > 0 then
            -- set_values() only stores flat scalars, hence the "name=dir"
            -- encoding rather than a list of tables (it happens to match
            -- generate_reflection.py's own --module-path NAME=DIR form, so
            -- run_project_codegen forwards it unchanged).
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
                -- The engine root has to come through a target value, not
                -- os.scriptdir()/os.projectdir(): inside a callback reached from
                -- a cross-repo includes(), BOTH resolve to the top-level
                -- (consumer) project rather than to this file. Only the
                -- description-scope FEATHER_ROOT captured above is trustworthy.
                feather_codegen.run_project_codegen(
                    table.wrap(target:values("feather.root"))[1],
                    os.projectdir(),
                    table.wrap(target:values("feather.codegen_dirs")),
                    {extensions = table.wrap(target:values("feather.codegen_extensions"))})
            end)
        end

        -- Fail early and readably when the engine hasn't been built. Consumers
        -- link against the engine EXECUTABLE and (because run_project_codegen
        -- passes --skip-core) rely on core's .gen.h files already existing in
        -- the engine checkout -- so "engine built first" is a hard prerequisite,
        -- and without this check it surfaces as an opaque linker or
        -- missing-header error. assert() IS available here: only description
        -- scope lacks it.
        set_values("feather.engine_bin", engine_bin)
        on_load(function(target)
            local bin = table.wrap(target:values("feather.engine_bin"))[1]
            local feather_root = table.wrap(target:values("feather.root"))[1]
            assert(os.isfile(bin) or (is_plat("windows") and os.isfile(bin .. ".exe")),
                "[feather] engine binary not found: " .. bin ..
                "\n[feather] Build FeatherEngine first (cd " .. feather_root .. " && xmake)," ..
                "\n[feather] or point feather_sdk_setup's engine_bin_dir at your engine build dir.")
        end)

        -- Same warning/optimisation flags the engine compiles itself with. The
        -- important one is -Wno-attributes: this project's own FCLASS/FSTRUCT
        -- headers carry bare [[get]]/[[set]]/[[method]] attributes that only the
        -- generator understands.
        on_config(function(target)
            import("feather_flags")
            feather_flags.apply(target)
        end)

        if is_plat("windows") then
            -- feather.lib is produced automatically by MSVC/clang-cl as a
            -- side effect of feather.exe exporting its declared FEATHER_API
            -- surface (see core/framework/feather_api.h and
            -- xmake/engine.lua's feather target) -- no .def file needed.
            add_linkdirs(bin_dir)
            add_links("feather")
        else
            -- Nothing to link against on ELF/Mach-O, deliberately: the DLL is
            -- left with undefined engine and flecs/SDL symbols, and the loader
            -- binds them to the already-running host executable at dlopen()
            -- time. That's what add_ldflags("-rdynamic") on feather (root
            -- xmake.lua) exists for, and what xmake/public_api.lua's
            -- {links = {}} sets up.
            --
            -- Do NOT "fix" this by linking engine_bin via add_shflags: ld will
            -- happily consume the PIE executable as a library input and stamp a
            -- DT_NEEDED for it into the DLL, at which point dlopen() goes
            -- hunting for a *file* called feather on the library search path
            -- instead of reusing the process's own image. (The previous
            -- add_ldflags(engine_bin) here never had any effect either way:
            -- languages/c++/xmake.lua maps target kinds to flag names as
            -- {binary = "ldflags", static = "arflags", shared = "shflags"}, so
            -- a shared target reads shflags and ignores ldflags entirely.)
            if is_plat("macosx") then
                -- Mach-O, unlike ELF, rejects undefined symbols in a dylib by
                -- default. Untested -- no macOS engine build has been run.
                add_shflags("-undefined", "dynamic_lookup", {force = true})
            end
        end
    target_end()
end
