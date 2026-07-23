# Migrate Feather Build System: CMake → xmake

## Context

The CMake build system has accumulated several structural pain points:

1. **Editor/Standalone duplication**: All core sources compile twice with no shared object files. `RegisterThirdparty()` calls `target_link_libraries(Editor ...)` and `target_link_libraries(Standalone ...)` directly, creating tight coupling between the thirdparty layer and the two top-level targets.
2. **Module thirdparty coupling**: `feather_module()` links `${THIRDPARTIES}` (the global accumulator) into every module, so every module gets every dependency even if it uses one.
3. **SDK is brittle**: `tools/generate_export.cmake` (180 lines) hand-generates `FeatherConfig.cmake` with hardcoded `_deps/` paths into the build tree — it breaks if paths move and is hard to keep in sync.
4. **Thirdparty boilerplate**: FetchContent + `RegisterThirdparty()` accumulator is verbose. Vex bypasses the pattern entirely and requires `vex_setup_runtime()` to be called manually from the root after modules are loaded.
5. **Development/RelWithDebInfo swap**: `thirdparty/CMakeLists.txt` swaps `CMAKE_BUILD_TYPE` to/from `RelWithDebInfo` to satisfy thirdparties, which is fragile.

xmake addresses all of these: Lua gives first-class functions for the module pattern, `add_packages()` per-target eliminates the global dep list, `xrepo` replaces FetchContent boilerplate, and `xmake install`+a small `FeatherSDK.lua` replaces `generate_export.cmake`.

---

## Approach: Phased cutover

Write all xmake files first, verify `xmake build` works end-to-end, then delete CMake files. No parallel maintenance period after the xmake build is green.

---

## New File Structure

```
xmake.lua                          <- root (replaces CMakeLists.txt + CMakePresets.json)
xmake/
  options.lua                      <- option declarations (replaces tools/options.cmake)
  helper.lua                       <- feather_module_target() helper function
thirdparty/
  xmake.lua                        <- add_requires() (replaces thirdparty/CMakeLists.txt)
  SimpleMath/
    xmake.lua                      <- local static target (was implicit in thirdparty glob)
  packages/
    vex.lua                        <- custom package with patch (replaces FetchContent+patch)
    taywee_args.lua                <- custom package if not in xrepo
modules/
  xmake.lua                        <- auto-discovery (replaces modules/CMakeLists.txt)
  vex_renderer/
    xmake.lua                      <- (replaces modules/vex_renderer/CMakeLists.txt)
tools/
  FeatherSDK.lua                   <- SDK helper for external consumers (replaces generate_export.cmake)
```

Files deleted after migration:
- `CMakeLists.txt`, `CMakePresets.json`, `CMakeUserPresets.json`
- `tools/options.cmake`, `tools/generate_export.cmake`, `tools/toolchain-llvm.cmake`, `tools/toolchain-mingw64.cmake`
- All `*/CMakeLists.txt`

---

## Build Mode Mapping

| CMake config | xmake mode | Notes |
|---|---|---|
| Debug | `debug` | O0, BETA, no NDEBUG |
| Development | `releasedbg` | O2, symbols, BETA, **must remove NDEBUG** (xmake adds it by default in this mode) |
| Release | `release` | O3, NDEBUG, PRODUCTION |

xmake CLI equivalents for CMakePresets:
```
xmake f -m debug                           # was: cmake --preset multi-debug
xmake f -m releasedbg                      # was: cmake --preset multi-development
xmake f -m release                         # was: cmake --preset multi-release
xmake f -m debug --toolchain=llvm          # was: cmake --preset multi-llvm (debug)
xmake f --toolchain=clang-cl               # native Windows (VS2022 + ClangCL)
xmake project -k vsxmake2022               # VS solution generation
xmake project -k compile_commands          # clangd / clang-tidy
```

### Cross-compilation is auto-detected (xmake/cross.lua)

When the target platform/arch differs from the host, `feather_setup_cross()`
(called from root `xmake.lua`) picks a cross toolchain automatically — no
`--toolchain` needed:
```
xmake f -p windows -a x64 --sdk=<msvc-wine sdk>   # -> msvc-wine (clang-cl, MSVC ABI)
xmake f -p linux -a arm64                          # -> muslcc (auto-downloaded)
```
mingw/llvm-mingw are intentionally not used (GNU ABI). macOS-from-Linux prints a
notice and stays on the host toolchain. An explicit `--toolchain=...` overrides.
See `CROSS_COMPILE_FINDINGS.md` (the "IMPLEMENTED" section) for the WinSDK
find_library fix and the root-scope requirement for `set_toolchains`.

---

## Step 1: `xmake/options.lua`

Declares all options (replaces `tools/options.cmake`). Toolchain selection moves to CLI flags (`--toolchain=llvm`, `--toolchain=mingw`, `--plat=android`); these options below are engine feature flags only.

```lua
option("use_lto");     set_default(false);                  set_description("Link-time optimization")           option_end()
option("static_cpp");  set_default(not is_plat("macosx")); set_description("Static C++ runtime")               option_end()
option("static_deps"); set_default(true);                   set_description("Static thirdparty deps")           option_end()
option("production");  set_default(false);                  set_description("Production (implies lto+static)")  option_end()
option("enable_sanitizers"); set_default(false); set_description("ASan+UBSan (debug+llvm only)") option_end()
option("enable_clang_tidy"); set_default(false); set_description("Run clang-tidy") option_end()
```

---

## Step 2: `xmake/helper.lua` — `feather_module_target()`

Replaces the CMake `feather_module()` macro. Creates `{name}_editor` and `{name}_standalone` static libs, then re-opens `feather.editor`/`feather.standalone` targets to add deps. Re-opening targets after initial declaration is valid in xmake.

```lua
function feather_module_target(name, module_dir, files)
    for _, variant in ipairs({"editor", "standalone"}) do
        target(name .. "_" .. variant)
            set_kind("static")
            set_warnings("none")           -- suppress thirdparty-inherited warnings
            set_group("modules")
            for _, f in ipairs(files or {}) do
                add_files(path.join(module_dir, f))
            end
            add_defines(name .. "_ENABLED")
            add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
            if is_mode("debug", "releasedbg") then add_defines("BETA") end
            add_includedirs(os.projectdir(), {public = true})
            add_includedirs(path.join(os.projectdir(), "core"), {public = true})
            add_includedirs(module_dir, {public = false})
            add_deps("feather_public_api") -- engine includes + public thirdparty headers
        target_end()
    end
    -- Wire into executables (targets already exist at this point)
    for _, variant in ipairs({"editor", "standalone"}) do
        target("feather." .. variant)
            add_deps(name .. "_" .. variant)
        target_end()
    end
end
```

`feather_public_api` is a `headeronly` target defined in root `xmake.lua` that carries the engine's public include dirs and PUBLIC thirdparty packages (directxmath, simplemath, flecs). This breaks the circular-dep cycle: modules can't depend on the executables they'll be linked into, but they can depend on this umbrella target.

---

## Step 3: `thirdparty/packages/vex.lua`

Custom package replicating FetchContent + the `VkRHI.cpp` patch:

```lua
package("vex")
    set_urls("<vex-github-url>", {branch = "main"})  -- fill in actual URL
    add_deps("cmake")
    on_install(function(package)
        -- Patch: remove VK_LAYER_KHRONOS_synchronization2 line from VkRHI.cpp
        local vkrhi = path.join(package:sourcedir(), "src", "Vulkan", "RHI", "VkRHI.cpp")
        if os.isfile(vkrhi) then
            local content = io.readfile(vkrhi)
            local patched, n = content:gsub('layers%.push_back%("VK_LAYER_KHRONOS_synchronization2"%);%s*\n?', "")
            if n > 0 then io.writefile(vkrhi, patched) end
        end
        import("package.tools.cmake").install(package, {["VEX_ENABLE_SLANG"] = "ON"})
    end)
package_end()
```

**Note**: Inspect what `vex_setup_runtime()` actually copies (shaders, DLLs next to the exe) from the Vex CMakeLists.txt. Replicate with `after_build` `os.cp()` calls on `feather.editor` and `feather.standalone`.

---

## Step 4: `thirdparty/xmake.lua`

Eliminates the `THIRDPARTIES` accumulator. Each target declares its own `add_packages()`.

```lua
add_repositories("feather-local " .. path.join(os.scriptdir(), "packages"))

-- Check xrepo for "args" first: xrepo search args
-- If not found, taywee_args.lua provides a headeronly package def with ARGS_NOEXCEPT define.

add_requires("flecs 4.1.5",   {system = false})
add_requires("assimp 6.0.4",  {system = false, configs = {shared = false, build_tools = false, build_tests = false}})
add_requires("sdl3 3.4.0",    {system = false, configs = {shared = not has_config("static_deps")}})
add_requires("directxmath",   {system = false})  -- header-only
add_requires("taywee_args",   {system = false})  -- swap for "args" if found in xrepo

if not is_plat("macosx") then
    add_requires("vex", {system = false})
end

includes(path.join(os.scriptdir(), "SimpleMath", "xmake.lua"))
```

`SimpleMath/xmake.lua` defines a static `simplemath` target with `add_deps("directxmath", {public=true})`, correct include dirs, and `set_warnings("none")`.

---

## Step 5: Root `xmake.lua`

Key design decisions:
- `before_build(run_codegen)` runs both Python scripts before compilation. Since the Python scripts use `write_if_changed()`, repeated runs are cheap.
- `remove_defines("NDEBUG")` in `releasedbg` mode is required — xmake adds it automatically in this mode, but CMake's Development config does not define it.
- `feather_public_api` headeronly target breaks the module→executable circular dep.
- Core source list mirrors `FEATHER_CORE_SOURCES` in the old `CMakeLists.txt` exactly.
- `modules/modules.gen.cpp` is hand-authored (despite the `.gen` extension) — include it as a regular source file.
- `includes("modules/xmake.lua")` comes at the end so `feather_module_target()` can re-open the already-declared executor targets to add module deps.

---

## Step 6: `modules/xmake.lua`

```lua
-- feather_module_target() is in scope from xmake/helper.lua loaded by root xmake.lua

for _, dir in ipairs(os.dirs(path.join(os.scriptdir(), "*"))) do
    local sub = path.join(dir, "xmake.lua")
    if os.isfile(sub) then includes(sub) end
end
```

## Step 7: `modules/vex_renderer/xmake.lua`

```lua
option("enable_vex_renderer")
    set_default(not is_plat("macosx"))
    set_description("Enable vex_renderer module")
option_end()

if has_config("enable_vex_renderer") then
    feather_module_target("vex_renderer", os.scriptdir(), {
        "register_module.cpp",
        "vex_renderer.cpp",
    })

    for _, variant in ipairs({"editor", "standalone"}) do
        target("vex_renderer_" .. variant)
            add_packages("vex", {public = false})
            if is_mode("releasedbg") then
                add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=1", "VEX_SHIPPING=0")
            elseif is_mode("release") then
                add_defines("VEX_DEBUG=0", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=1")
            else
                add_defines("VEX_DEBUG=1", "VEX_DEVELOPMENT=0", "VEX_SHIPPING=0")
            end
        target_end()
    end
    -- TODO: add after_build vex_setup_runtime() equivalent once Vex source is inspected
end
```

---

## WINDOWS_EXPORT_ALL_SYMBOLS Gap

CMake auto-generates a `.def` export file from the executable so runtime-loaded plugin DLLs can resolve engine symbols. xmake's `utils.symbols.export_all` rule is shared-library only.

**Phase 1 (immediate)**: Build once with CMake, extract exports:
```
dumpbin /EXPORTS build/bin/feather.editor.exe > tools/feather.editor.def
```
Trim to just the `EXPORTS` section. Commit `tools/feather.editor.def` and `tools/feather.standalone.def`.

Root `xmake.lua` already has the `is_plat("windows")` ldflags wiring in place (guarded by `os.isfile()`, so it's a no-op until the `.def` files below are committed):
```lua
if is_plat("windows") then
    local def_file = path.join(os.scriptdir(), "tools", "feather." .. variant .. ".def")
    if os.isfile(def_file) then
        add_ldflags("/DEF:" .. def_file, {force = true})
    end
end
```
**Still outstanding**: `tools/feather.editor.def` / `tools/feather.standalone.def` themselves are not yet committed — generating them requires a Windows build (either the old CMake build while it still exists, or an xmake build plus `dumpbin`). This is the one piece of the SDK plumbing that could not be verified in a Linux-only environment; the Linux link path (`add_ldflags` with the raw exe path + `-rdynamic`, see Step 8) is fully wired and testable today. Update the `.def` files whenever the public API surface changes.

**Phase 2 (long-term)**: Introduce `FEATHER_API __declspec(dllexport/import)` decorators on all symbols modules call across the DLL boundary. This removes the need for `.def` files and clarifies the engine's plugin ABI.

---

## Step 8: SDK — `tools/FeatherSDK.lua`

Replaces `generate_export.cmake` (180 lines). `feather_public_api` (public include dirs + PUBLIC thirdparty packages) was extracted out of root `xmake.lua` into its own file, `xmake/public_api.lua`, so it can be `includes()`'d by both the root build and this file — the single source of truth for the engine's public API surface. `FeatherSDK.lua` itself never hand-lists include dirs or packages; it just depends on `feather_public_api`, so a new public thirdparty dependency (or a module opting into SDK exposure by re-opening `feather_public_api` to add itself) never requires editing this file:

```lua
-- tools/FeatherSDK.lua
local FEATHER_ROOT = path.directory(os.scriptdir())  -- one level up from tools/

-- Same include order as root xmake.lua, so package configs (static_deps
-- etc.) hash-match the engine's own already-built xrepo cache instead of
-- xrepo building a second variant just for the consumer.
includes(path.join(FEATHER_ROOT, "xmake", "options.lua"))
includes(path.join(FEATHER_ROOT, "thirdparty", "xmake.lua"))
includes(path.join(FEATHER_ROOT, "xmake", "public_api.lua"))

function feather_sdk_setup(target_name, variant)
    variant = variant or "standalone"
    target(target_name)
        add_defines("EDITOR_BUILD=" .. (variant == "editor" and "1" or "0"))
        add_deps("feather_public_api")
        local bin_dir = path.join(FEATHER_ROOT, "build", "bin")
        if is_plat("windows") then
            add_linkdirs(bin_dir)
            add_links("feather." .. variant)
        else
            -- add_links("feather.editor") never resolves on Linux: gcc/clang
            -- only treat a link name as an exact filename when it ends in
            -- .a/.so, otherwise "-lfeather.editor" searches for
            -- libfeather.editor.so. Link by exact raw path instead.
            add_ldflags(path.join(bin_dir, "feather." .. variant), {force = true})
        end
    target_end()
end
```

Consumer (game project) usage — a full copy-paste starting point lives at `tools/templates/consumer_xmake_template.lua`, including the discovery block below. Note `feather_sdk_setup()` opens and closes its own `target(...)...target_end()` scope internally, so the consumer's own `set_kind`/`add_files` must go in a **separate, reopened** block afterward, not nested inside one shared block (mirrors how `modules/vex_renderer/xmake.lua` reopens `vex_renderer_editor`/`_standalone` after `feather_module_target()` already declared them):
```lua
-- mygame/xmake.lua
option("feather_sdk_path") ... option_end()          -- see template for the full
local function resolve_feather_root() ... end        -- discovery block: config
                                                       -- option -> feather_dir.txt
                                                       -- -> FEATHER_ROOT env var.
                                                       -- error()/raise()/assert()
                                                       -- are all unavailable at
                                                       -- description scope, so an
                                                       -- unresolved root prints
                                                       -- guidance instead of
                                                       -- hard-failing here.
local FEATHER_ROOT = resolve_feather_root()
if FEATHER_ROOT then
    includes(path.join(FEATHER_ROOT, "tools", "FeatherSDK.lua"))
end

feather_sdk_setup("mygame_plugin", "standalone")

target("mygame_plugin")          -- SEPARATE block, after feather_sdk_setup()
    set_kind("shared")
    add_files("src/*.cpp")
target_end()
```

No CMake package registry, no hardcoded `_deps/` paths, no multi-config generation boilerplate.

---

## Execution Order

1. `xmake/options.lua` + `xmake/helper.lua` — no build impact
2. `thirdparty/packages/vex.lua` + `taywee_args.lua` — custom package definitions
3. `thirdparty/SimpleMath/xmake.lua` — local static target
4. `thirdparty/xmake.lua` — package declarations; run `xmake f -m debug` to verify xrepo downloads
5. Root `xmake.lua` with only the two executables (no modules yet); confirm `xmake build feather.editor` compiles
6. `modules/vex_renderer/xmake.lua` + `modules/xmake.lua`; build again with modules
7. Resolve `WINDOWS_EXPORT_ALL_SYMBOLS` gap (Phase 1: extract `.def` from CMake build while it still exists)
8. `tools/FeatherSDK.lua`; validate with a toy consumer `includes()` test
9. Delete all CMake files once xmake build is CI-green

---

## Verification

```bash
xmake f -m debug && xmake                   # debug build, both targets
xmake f -m releasedbg && xmake              # development equivalent
xmake f -m release && xmake                 # release
xmake f -m debug --toolchain=llvm && xmake  # LLVM toolchain
xmake run feather.editor                     # smoke test launch
# Check generated files exist after first build:
ls core/**/register_*.gen.cpp
ls raw_resources/**/*.gen.h
# Verify BETA is defined in debug/releasedbg and absent in release
# Verify NDEBUG is absent in debug and releasedbg, present in release
```
