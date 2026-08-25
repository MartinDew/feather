-- Auto-discover every subdirectory that has an xmake.lua and include it.
for _, dir in ipairs(os.dirs(path.join(os.scriptdir(), "*"))) do
    local sub = path.join(dir, "xmake.lua")
    if os.isfile(sub) then
        includes(sub)
    end
end
