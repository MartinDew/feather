-- Auto-load all custom package definitions from packages/
-- Adding a new thirdparty: drop a <name>.lua file in thirdparty/packages/ and add_requires() here.
for _, pkg_file in ipairs(os.files(path.join(os.scriptdir(), "packages", "*.lua"))) do
    includes(pkg_file)
end

-- ---- Package mode (debug vs release) ------------------------------------
-- In CMake the Development config maps to RelWithDebInfo, which means
-- thirdparties build in Release mode. Mirror that here: only use debug
-- packages when in debug mode.
if is_mode("debug") then
    add_requireconfs("*", {debug = true})
end

-- ---- Core packages (all available in xrepo) -----------------------------
-- flecs ECS framework
add_requires("flecs 4.1.5", {
    system = false,
    alias  = "flecs",
    configs = {shared = not has_config("static_deps")},
})

-- assimp asset importer
-- Uses a local package definition (thirdparty/packages/assimp.lua) that passes
-- -DASSIMP_BUILD_MINIZIP=ON so assimp compiles its own contrib/unzip/ instead
-- of relying on system minizip (whose pkgconfig omits the include subdir).
add_requires("assimp 6.0.4", {
    system = false,
    alias  = "assimp",
    configs = {
        shared    = not has_config("static_deps"),
        no_export = true,
        -- Debug build fails due to a PDB error in assimp's CMakeLists.txt; force release
        debug     = false,
    },
})

-- ---- Custom packages (local definitions in packages/) -------------------

-- DirectXMath (header-only, apr2025 tag, includes local sal.h shim)
add_requires("directxmath_feather", {
    system = false,
    alias  = "directxmath",
})

-- SDL3 (VIDEO subsystem only, static or shared based on static_deps)
add_requires("sdl3_feather", {
    system = false,
    alias  = "sdl3",
    configs = {shared = not has_config("static_deps")},
})

-- Vex (Vulkan rendering, not on Apple platforms)
if not is_plat("macosx") then
    add_requires("vex", {
        system = false,
        alias  = "vex",
    })
end

-- DirectX 12 Agility SDK redistributable (D3D12Core.dll, d3d12SDKLayers.dll).
-- Vex's own CMake build fetches its own internal copy to compile Vex.lib against
-- (unavoidable, baked into Vex's VexDX12.cmake), but we source the DLLs we actually
-- deploy from this official xrepo package instead of reaching into Vex's internal
-- FetchContent _deps/ layout, which is an implementation detail that could change.
-- Version must match Vex's vendored DIRECTX_AGILITY_SDK_VERSION (currently 618,
-- see modules/vex_renderer/xmake.lua). xrepo doesn't carry the exact 1.618.4 patch
-- Vex vendors; the middle digit (618) is the actual SDK identifier D3D12SDKVersion
-- has to match, so the closest available 1.618.x patch is used instead.
if is_plat("windows") then
    add_requires("directx12-agility 1.618.1", {
        system = false,
        alias  = "directx12-agility",
    })
end

-- ---- SimpleMath: local static target (not an xrepo package) -------------
includes(path.join(os.scriptdir(), "SimpleMath", "xmake.lua"))
