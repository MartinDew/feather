-- FeatherSDK.lua: the plugin ABI's single entry point for a downstream
-- project (docs/plugin_abi.md). Usage: see
-- tools/templates/consumer_xmake_template.lua.
--
-- A plugin needs no engine library to link against and no whole-tree
-- include path -- core/extension/feather_interface.h is pure C with no
-- engine header behind it, and every engine function it calls is resolved
-- by name at load time, not by the linker. This file only wires up
-- discovery-independent codegen (feather_codegen.run_binding_codegen) and a
-- flat build target; engine-root discovery lives in the CONSUMER's own
-- xmake.lua (can't be includes()'d from here -- chicken-and-egg).
local FEATHER_ROOT = path.directory(path.directory(os.scriptdir()))

-- Lets a consumer import() feather_codegen from its own project.
add_moduledirs(path.join(FEATHER_ROOT, "xmake", "modules"))

-- opts.engine_bin_dir: defaults to <engine>/build/bin.
-- opts.gen_dir: defaults to <projectdir>/.gen.
function feather_sdk_setup(target_name, opts)
    opts = opts or {}

    local bin_dir = opts.engine_bin_dir or path.join(FEATHER_ROOT, "build", "bin")
    local engine_bin = path.join(bin_dir, "feather")
    local gen_dir = opts.gen_dir or path.join(os.projectdir(), ".gen")

    target(target_name)
        set_kind("shared")
        -- Flat, not bin/$(mode): the engine finds this .so by recursively
        -- scanning the project dir, so a per-mode subdir would leave stale
        -- copies for it to load a second time.
        set_targetdir(path.join(os.projectdir(), "bin"))

        add_includedirs(path.join(FEATHER_ROOT, "core"))
        add_includedirs(gen_dir)

        -- before_build/on_load closures run sandboxed and can't see this
        -- function's locals, so hand the resolved paths over as target values.
        set_values("feather.root", FEATHER_ROOT)
        set_values("feather.engine_bin", engine_bin)
        set_values("feather.gen_dir", gen_dir)

        before_build(function(target)
            local root = table.wrap(target:values("feather.root"))[1]
            local bin = table.wrap(target:values("feather.engine_bin"))[1]
            local gen = table.wrap(target:values("feather.gen_dir"))[1]
            assert(os.isfile(bin) or (is_plat("windows") and os.isfile(bin .. ".exe")),
                "[feather] engine binary not found: " .. bin ..
                "\n[feather] Build FeatherEngine first (cd " .. root .. " && xmake).")
            import("feather_codegen")
            feather_codegen.run_binding_codegen(root, bin, gen)
        end)
    target_end()
end
