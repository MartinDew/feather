-- Custom package definitions live in packages/; add_requires() each one below.
for _, pkg_file in ipairs(os.files(path.join(os.scriptdir(), "packages", "*.lua"))) do
    includes(pkg_file)
end

-- CMake's Development config mapped to RelWithDebInfo, so thirdparties built in
-- release mode; mirror that by only requesting debug packages in debug mode.
if is_mode("debug") then
    add_requireconfs("*", {debug = true})
end

-- windows/mingw force shared regardless of static_deps: no -rdynamic there,
-- so a static flecs would duplicate ecs_os_api per binary.
local FEATHER_FORCE_SHARED_DEPS = is_plat("windows", "mingw")
add_requires("flecs 4.1.5", {
    system = false,
    alias  = "flecs",
    configs = {shared = FEATHER_FORCE_SHARED_DEPS or not has_config("static_deps")},
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
-- SDL3 owns process-global state too; same reasoning as flecs above.
add_requires("sdl3_feather", {system = false, alias = "sdl3", configs = {shared = FEATHER_FORCE_SHARED_DEPS or not has_config("static_deps")}})

if not is_plat("macosx") then
    add_requires("vex", {system = false, alias = "vex"})
end

-- Version must track Vex's vendored DIRECTX_AGILITY_SDK_VERSION (618); xrepo
-- doesn't carry the exact patch Vex vendors, so use the closest 1.618.x.
if is_plat("windows") then
    add_requires("directx12-agility 1.618.1", {system = false, alias = "directx12-agility"})
end

if is_plat("mingw") then
    add_requires("vulkan-headers 1.4.335+0", {system = false, alias = "vulkan-headers"})
    add_requires("vulkan-loader 1.4.335+0", {system = false, alias = "vulkan-loader"})
end

-- Local package (packages/mrbind.lua): a Clang-based C++ parser and binding
-- generator, a host tool nothing links against. Required here so it's built
-- at `xmake f` time -- but only when a bindings module actually needs it,
-- since without a system Clang install the package builds LLVM from source.
if has_config("enable_c_bindings", "enable_cs_bindings", "enable_py_bindings") then
    add_requires("mrbind", {system = false, alias = "mrbind"})

    -- Windows always takes mrbind's libllvm path (see packages/mrbind.lua's
    -- on_load). Required directly, not just transitively, so the bindings
    -- modules get a usable target:pkg("libllvm") handle for resolving the
    -- clang mrbind was built with (see xmake/modules/feather_bindings.lua).
    if is_plat("windows") then
        add_requires("libllvm", {system = false, alias = "libllvm", configs = {shared = false, clang = true}})
    end
end

-- Header-only; the generated Python module's macro TU compiles against it.
if has_config("enable_py_bindings") then
    add_requires("pybind11", {system = false, alias = "pybind11"})
end

includes(path.join(os.scriptdir(), "SimpleMath", "xmake.lua"))
