-- feather_module_target() is in scope from xmake/helper.lua loaded by the root xmake.lua.
-- Auto-discover every subdirectory that has an xmake.lua and include it.
-- New modules appear automatically; no manual registration needed.
for _, dir in ipairs(os.dirs(path.join(os.scriptdir(), "*"))) do
    local sub = path.join(dir, "xmake.lua")
    if os.isfile(sub) then
        includes(sub)
    end
end
