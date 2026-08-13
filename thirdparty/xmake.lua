-- Custom package definitions live in packages/; add_requires() each one below.
for _, pkg_file in ipairs(os.files(path.join(os.scriptdir(), "packages", "*.lua"))) do
    includes(pkg_file)
end

-- CMake's Development config mapped to RelWithDebInfo, so thirdparties built in
-- release mode; mirror that by only requesting debug packages in debug mode.
if is_mode("debug") then
    add_requireconfs("*", {debug = true})
end

-- Local package (packages/flecs.lua): builds flecs with default rather than
-- hidden symbol visibility, so -rdynamic can re-export ecs_* to project DLLs.
-- Stock xrepo flecs hides those symbols and crashes project DLLs on startup.
add_requires("flecs_feather 4.1.5", {
    system = false,
    alias  = "flecs",
    configs = {shared = not has_config("static_deps")},
})

-- Local package (packages/assimp.lua) builds assimp's bundled minizip instead of
-- the system one, whose pkgconfig omits the include dir on Linux.
add_requires("assimp 6.0.4", {
    system = false,
    alias  = "assimp",
    configs = {
        shared    = not has_config("static_deps"),
        no_export = true,
        debug     = false, -- assimp's CMakeLists has a PDB bug in debug
    },
})

add_requires("directxmath_feather", {system = false, alias = "directxmath"})
add_requires("sdl3_feather", {system = false, alias = "sdl3", configs = {shared = not has_config("static_deps")}})

if not is_plat("macosx") then
    add_requires("vex", {system = false, alias = "vex"})
end

-- DLLs deployed at runtime come from this xrepo package rather than Vex's internal
-- CMake fetch. Version must track Vex's vendored DIRECTX_AGILITY_SDK_VERSION (618);
-- xrepo doesn't carry the exact 1.618.4 patch Vex vendors, so use the closest 1.618.x.
if is_plat("windows") then
    add_requires("directx12-agility 1.618.1", {system = false, alias = "directx12-agility"})
end

includes(path.join(os.scriptdir(), "SimpleMath", "xmake.lua"))