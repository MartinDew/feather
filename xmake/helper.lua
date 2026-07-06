-- feather_module_target(name, module_dir, files)
--
-- Creates {name}_editor and {name}_standalone static library targets, then
-- re-opens feather.editor / feather.standalone to add them as deps.
-- Re-opening targets after their initial declaration is valid in xmake.
--
-- Prerequisites:
--   - feather.editor and feather.standalone must already be declared.
--   - feather_public_api must already be declared (provides engine includes
--     and public thirdparty headers without creating a circular dep).
--   - The caller's xmake.lua must have already checked has_config("enable_<name>").
--
-- name       : module name, e.g. "vex_renderer"
-- module_dir : os.scriptdir() from the calling xmake.lua
-- files      : list of .cpp filenames relative to module_dir

function feather_module_target(name, module_dir, files)
    for _, variant in ipairs({"editor", "standalone"}) do
        target(name .. "_" .. variant)
            set_kind("static")
            set_warnings("none")
            set_group("modules")
            for _, f in ipairs(files or {}) do
                add_files(path.join(module_dir, f))
            end
            add_defines(name .. "_ENABLED", {public = true})
            add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
            if is_mode("debug", "releasedbg") then
                add_defines("BETA")
            end
            -- Public include dirs so consumers of this module see engine headers
            add_includedirs(os.projectdir(), {public = true})
            add_includedirs(path.join(os.projectdir(), "core"), {public = true})
            -- Private: module's own header dir
            add_includedirs(module_dir, {public = false})
            -- Pull in engine public API (includes + public thirdparty headers)
            -- without depending on the executables (which would be circular)
            add_deps("feather_public_api")
        target_end()
    end

    -- Wire module variants into the main executables.
    -- These targets already exist; reopening them just adds deps.
    for _, variant in ipairs({"editor", "standalone"}) do
        target("feather." .. variant)
            add_deps(name .. "_" .. variant)
        target_end()
    end
end
