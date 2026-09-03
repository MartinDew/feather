-- Custom package definitions live in packages/; add_requires() each one below.
for _, pkg_file in ipairs(os.files(path.join(os.scriptdir(), "packages", "*.lua"))) do
    includes(pkg_file)
end

-- DirectXMath's package lives in the SDK instead, next to the SimpleMath
-- sources it supplies a sal.h shim for: a plugin vendors both and builds the
-- same math types the engine did, which is what lets those types cross the C
-- boundary as themselves. One definition, used from both sides.
includes(path.join(path.directory(os.scriptdir()), "tools", "SDK", "packages", "directxmath.lua"))

-- CMake's Development config mapped to RelWithDebInfo, so thirdparties built in
-- release mode; mirror that by only requesting debug packages in debug mode.
if is_mode("debug") then
    add_requireconfs("*", {debug = true})
end

-- windows/mingw force shared regardless of static_deps: no -rdynamic there,
-- so a static flecs would duplicate ecs_os_api per binary.
local FEATHER_FORCE_SHARED_DEPS = is_plat("windows", "mingw")
-- flecs is shared everywhere, static_deps or not, and this is load-bearing for
-- extensions rather than a preference.
--
-- The plan elsewhere (see xmake/public_api.lua's {links = {}}) is that a
-- consumer DLL leaves flecs symbols undefined and binds them to the host
-- executable at dlopen time, thanks to -rdynamic. That does not work with the
-- static flecs: its archive is compiled with hidden visibility, so every flecs
-- symbol becomes LOCAL when linked into the executable and -rdynamic cannot
-- export what is no longer global. Confirmed by readelf: `ecs_init` is GLOBAL
-- HIDDEN in libflecs_static.a and LOCAL in the linked binary.
--
-- The result was that anything dlopen'd which touches flecs -- libfeather_c
-- above all, but equally a C++ extension that registers an EcsModule -- failed
-- to load with "undefined symbol: EcsOnLoad". A shared flecs is exported
-- normally and both the engine and the extension bind to the same copy, which
-- is also what keeps ecs_os_api single.
add_requires("flecs 4.1.5", {
    system = false,
    alias  = "flecs",
    configs = {shared = true},
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

-- Header-only; parses .fext extension manifests (core/resources/fext_format_loader.cpp).
-- Not in feather_public_api: it stays out of the engine's public headers.
add_requires("nlohmann_json", {system = false, alias = "nlohmann_json"})

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
if has_config("enable_c_bindings", "enable_cs_bindings", "enable_py_bindings", "enable_cpp_bindings") then
    -- host = true: mrbind is a build tool this machine runs, not something the
    -- engine links. Without it a cross-compile (mingw, say) would build the
    -- parser for the target and then be unable to execute it.
    --
    -- gen_cpp_rev is passed here rather than computed inside the package: a
    -- package's install hash comes from the configs the requiring side asks
    -- for, so a config the package sets itself would never invalidate it, and
    -- editing the grafted generator would silently keep the old binary.
    add_requires("mrbind", {system = false, alias = "mrbind", host = true,
        configs = {gen_cpp_rev = feather_gen_cpp_rev(path.join(path.directory(os.scriptdir()), "tools", "SDK", "gen_cpp"))}})

    -- Windows always takes mrbind's libllvm path (see packages/mrbind.lua's
    -- on_load). Required directly, not just transitively, so the bindings
    -- modules get a usable target:pkg("libllvm") handle for resolving the
    -- clang mrbind was built with (see xmake/modules/feather_bindings.lua).
    if is_plat("windows") then
        add_requires("libllvm", {system = false, alias = "libllvm", configs = {shared = false, clang = true}})
    end
end

-- Header-only; the generated Python module's macro TU compiles against it.
-- The platform check is here rather than in the option's default: is_plat()
-- reads as false inside an option scope (see xmake/options.lua).
if has_config("enable_py_bindings") and not is_plat("windows", "mingw") then
    add_requires("pybind11", {system = false, alias = "pybind11"})
end

includes(path.join(os.scriptdir(), "SimpleMath", "xmake.lua"))
