-- Custom package definitions live in packages/; add_requires() each one below.
for _, pkg_file in ipairs(os.files(path.join(os.scriptdir(), "packages", "*.lua"))) do
    includes(pkg_file)
end

-- CMake's Development config mapped to RelWithDebInfo, so thirdparties built in
-- release mode; mirror that by only requesting debug packages in debug mode.
if is_mode("debug") then
    add_requireconfs("*", {debug = true})
end

-- Stock xrepo package. Used to be a local shadow package (packages/flecs.lua)
-- that patched flecs's CMake to build with default rather than hidden symbol
-- visibility, so the -rdynamic engine executable could re-export ecs_* for
-- project DLLs to bind against at dlopen() time -- see xmake/public_api.lua
-- for why that's no longer needed: core/world/ecs_api.h/ecs_defs.h firewall
-- flecs out of a plugin's ABI entirely now, so nothing outside this binary
-- ever needs an ecs_* symbol, and flecs can go back to hiding them like any
-- other static dependency.
add_requires("flecs 4.1.5", {
    system = false,
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
