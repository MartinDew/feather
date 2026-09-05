# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Feather is an ECS-based game engine in C++23, built with xmake (not raw CMake/Make — CMake is only used internally to build vendored third-party sources like mrbind). ECS is via flecs.

## Comments

Comment blocks are at most two lines except for file headers. A comment explains how the system works — an invariant, a non-obvious reason something is the way it is — never what a change did or why it was made. No "fixed X", no "this used to be Y", no narrating the diff.

## Build

Configure once per toolchain/option change, then build:

```
xmake f -m debug -y                              # Linux/macOS, default toolchain
xmake f -m debug --toolchain=clang-cl -y         # Windows
xmake f -m debug --toolchain=llvm -y             # Linux/macOS, explicit LLVM/Clang
xmake f -m debug --toolchain=mingw --sdk=<path> -y  # MinGW cross-compile
xmake build                                       # build everything configured
xmake build feather                               # build just the engine executable
```

Modes: `debug` (-O0 + BETA), `releasedbg` (-O2 + BETA, no NDEBUG), `release` (-O3, NDEBUG, symbols stripped).

Turning a bindings option off and back on leaves the executable linked as it was: xmake does not treat the target that reappeared as a reason to relink, so `xmake build` reports success while `feather` still exports nothing. `xmake build -r` after such a toggle.

Reflection codegen (`tools/codegen/generate_reflection.py`) and embedded-resource codegen run automatically via a `before_build` hook — never invoke them by hand or edit `*.gen.h`/`*.gen.cpp` files directly.

Key build options (`xmake f --<option>=y|n`, see `xmake/options.lua` and `modules/*/xmake.lua`):
- `editor_build` (default on) — compiles editor-only registration (`InitLevel::Editor`); off builds shipping-only.
- `enable_c_bindings` / `enable_cs_bindings` / `enable_cpp_bindings` (default on) — generate the mrbind-based C/C#/C++ bindings for `core/`. The C# and C++ ones are generated from the C bindings' descriptor, so they need `enable_c_bindings` too.
- `enable_vex_renderer` (default on except macOS — Vex has no Metal backend).
- `use_lto`, `static_cpp`, `static_deps`, `production` (bundles the first three), `enable_sanitizers`, `enable_clang_tidy`.

`xmake export-api` publishes `build/bindings/dist/feather_api.json` — the API description external plugin projects build against (see Plugin system below). It rewrites the engine checkout's own path and DirectXMath's into the `@feather`/`@directxmath` tokens the SDK substitutes back, so the file names no machine. It must produce *only* that one JSON file; nothing binary.

A pre-commit hook (`.githooks/pre-commit`) runs `clang-format` on staged C/C++ (excluding `*.gen.h`/`*.gen.cpp` and `thirdparty/`). It isn't wired in by default — run `git config core.hooksPath .githooks` once to enable it.

## Running

```
./build/bin/feather <project_dir> -w headless --run-frames 5   # run N real frames then exit cleanly
./build/bin/feather <project_dir> -w headless --dump-db        # print ClassDB and exit (editor builds only; exits before the frame loop, so use --run-frames if you need anything from a running world)
./build/bin/feather <project_dir> -e                            # editor mode (editor builds only)
```

## Testing

There is no unit test suite in this repository. Integration testing lives in the sibling repo `feather-example-project` (a separate checkout, e.g. `../feather-example-project`), whose `examples/run_examples_test.sh` builds C/C#/C++ example extensions against a built engine and asserts on the log output of a single headless `--run-frames` run. Run it after any change to bindings, the plugin/resource-loading system, or reflection:

```
cd ../feather-example-project && examples/run_examples_test.sh                 # finds a sibling engine checkout
examples/run_examples_test.sh /path/to/FeatherEngine                            # or name one explicitly
```

CI (`.github/workflows/ci.yml`) matrixes Linux (gcc) and Windows (clang-cl) × {debug, releasedbg, release}: builds the engine, checks the bindings output and exported symbol counts, runs `export-api`, then builds the plugin SDK's C, C++ and C# templates as standalone smoke tests (no engine checkout, just the exported API) to exercise what an external plugin author actually does, and asserts the resulting plugins need nothing from the engine but flat C symbols.

## Architecture

### Reflection (ClassDB / Variant)

C++ classes opt into runtime reflection with `FCLASS(...)` (polymorphic, `Reflected`-derived) or `FSTRUCT(...)` (`FCLASS(novtable)` — a plain value type, e.g. an ECS component) from `core/framework/reflection_macros.h`. Members are exposed via `[[get]]`/`[[set]]`/`[[name(...)]]`/`[[method]]` attributes read directly off the declaration. `tools/codegen/generate_reflection.py` parses headers for these macros/attributes and emits, per source directory: a `<header>.gen.h` (included last in the header, defines the class's `_bind_members()`/`get_class_static()`) and a `register_<dir>_types.gen.cpp` that calls `ClassDB::register_class<T>()`/`register_value_class<T>()` etc. Codegen behavior is pluggable via `tools/codegen/modifier_api.py` + `tools/codegen/extensions/` (`core_modifiers.py` for `singleton`/`abstract`; `ecs.py` for the `EcsModule`/`Component` modifiers that additionally wire a class into flecs).

`ClassDB` (`core/main/class_db.h`/`.inl`) is a runtime, string-keyed class registry: `register_class<T>()`, `bind_property*`, `bind_method*`, `create_object_unsafe(name)`, `on_subclass_registered(base, callback)` (fires when a matching subclass registers, including ones from extensions loaded after startup). `Variant` (`core/framework/variant.h`) is the type-erased value type reflection uses for property/method marshalling; `Callable` (`core/framework/callable.h`) type-erases method calls the same way.

Non-template registration also exists for types with no compile-time C++ type at all — `ClassDB::register_scripted_value_class` plus `core/world/scripted_component.h`/`scripted_system.h`, used so a plugin in any language can define genuinely new ECS component/system types at runtime (see Bindings below), not just call pre-registered ones.

### Startup staging (`InitLevel`)

`core/main/init_level.h`: `Core` (ClassDB exists, reflected types register) → `Servers` (rendering constructed) → `World` (ECS world exists, components/systems/modules register) → `Editor` (editor-only, entered only in editor builds). Modules and extensions hook in at the level appropriate to what they need.

### Modules

`modules/*/xmake.lua` are auto-discovered and `includes()`'d by `modules/xmake.lua`. A module uses `feather_module_target()` (`xmake/helper.lua`) — declares the module target, adds it to the `feather` target's deps, and defines a `<name>_ENABLED` compile-time flag. `opts.kind` picks between a static lib (the default), an `object` target for code the executable must keep even though nothing references it, and a `phony` one for a module that only generates files; `opts.deps` orders a module after another. Enabled modules are dispatched through the hand-maintained `modules/modules.gen.cpp`/`.gen.h` (`register_modules(InitLevel)`/`unregister_modules(InitLevel)`, `#if <name>_ENABLED` guards — not yet autogenerated, per its own comment).

The three bindings modules use those options: `c_bindings` is `object` because nothing in the engine references a generated `feather_c_*` symbol and a static archive would drop them all as unreferenced; `cs_bindings` and `cpp_bindings` are `phony` generators that depend on it for the descriptor they read — see Bindings below.

### ECS

flecs 4.1.5 (`thirdparty/xmake.lua`), aliased in `core/world/ecs_defs.h` (`World = flecs::world`, `Entity = flecs::entity`, `Ecs = flecs`). World-level features (rendering, math, core) are `EcsModule` subclasses, discovered reflectively via `ClassDB::get_children_names(EcsModule::get_class_static())` and imported through their generated `_import_module` static method — including ones registered after startup, via the same `on_subclass_registered` hook. Components are `FSTRUCT(Component)` types; the `ecs.py` codegen modifier emits the actual `world.component<T>()` registration separately per directory, since that needs a live `flecs::world&` neither `.gen.h` nor `_bind_members()` has access to. Component types are declared in `WorldSim`'s constructor (not `init()`) specifically so anything loaded during project indexing can already query them.

### Bindings (mrbind) and the plugin system

`xmake/bindings.lua` parses `core/` once with mrbind (pinned commit, `thirdparty/packages/mrbind.lua`) into `build/bindings/api.json`. `modules/c_bindings` generates C glue from it and compiles that into the engine itself (see Modules above) — a C or C# plugin therefore resolves `feather_*` symbols against the running engine process, the same way a C++ plugin resolves engine symbols, with no separate library for the engine to ship or load. `modules/cs_bindings` generates C# source from the C bindings' descriptor for external use. `modules/cpp_bindings` generates header-only C++ wrappers from that same descriptor with `tools/SDK/feather_cpp/gen_cpp` (a Feather-owned generator, built by grafting it into mrbind's own CMake tree — see its README), so a C++ plugin calls the engine through the same flat C symbols a C plugin does, with no shared C++ ABI; its `cpp_bindings_check` target compiles the result against nothing but the C headers and the vendored math, which is the property that lets a plugin build with no engine checkout. `modules/c_bindings/scripted_abi.h`'s flat C ABI is how a plugin in any language defines new ECS component/system types at runtime; a NativeAOT C# plugin and the C++ SDK header both call it.

Extensions are discovered by `ResourceLoader` indexing the project directory (`core/resources/resource_loader.*`) for a `.fext` JSON manifest (`core/resources/fext_format_loader.*` — names a native library plus an entry point, the only kind of extension there is). A manifest's `entry` names a plain `void(uint8_t)` C function, which is all a native plugin exports in any language — there is no path that hands a C++ object across the boundary, so no plugin shares the engine's C++ ABI.

`tools/SDK/` is meant to be **vendored into an external plugin repo**, not consumed from this checkout. `FeatherPluginSDK.lua`, `modules/feather_plugin_bindings.lua` and `packages/` are shared; each language then has its own directory holding what only it needs — `feather_c/`, `feather_cpp/` (the `gen_cpp/` generator, the hand-written `include/feather_cpp/` headers, DirectXMath's package, and the `thirdparty/` SimpleMath and DirectXMath sources both the engine and plugins compile) and `feather_cs/` (`FeatherPluginBootstrap.cs`). A plugin repo vendors the shared files plus the languages it builds; each directory defines that language's `feather_<lang>_plugin()`. Combined with a published `feather_api.json` (from `xmake export-api`), it's the whole toolchain a plugin author needs — no engine checkout, no Clang/LLVM (`mrbind_generators.lua` builds mrbind's generators only, `MRBIND_BUILD_PARSER=OFF`). `tools/templates/plugin_c_template/`, `plugin_cs_template/` and `plugin_cpp_template/` are starting points.

The math types are the one thing a C++ plugin does not reach through a wrapper: it compiles the same SimpleMath sources the engine did (from `tools/SDK/feather_cpp/thirdparty/`, the single copy both sides use), so `Vector2/3/4`, `Quaternion` and `Color` cross the C boundary as real structs and `Matrix` as a pointer to one. The generated `feather_cpp/math.hpp` asserts every size and field offset the engine published, so a layout disagreement is a compile error rather than reinterpreted memory. A C# plugin needs no hand-written entry point or P/Invoke resolver — `FeatherPluginBootstrap.cs` supplies both and finds `[FeatherComponent]`/`[FeatherSystem]`/`[FeatherInit]`-attributed types by reflecting over the assembly at a fixed, SDK-owned entry point.

`mrbind_generators.lua` is a `host = true` package — it always builds for whichever machine is running the build, never the project's configured target platform, so it must check `os.host()` rather than `package:is_plat()` for that. On Windows it explicitly prefers `clang-cl` over real `cl.exe` (found via `find_tool`, not left to CMake's own default toolchain search): mrbind's source uses C++23 features that current MSVC toolsets don't all support, clang-cl does regardless of which VS is installed, and it takes the same MSVC-style flags (`/Zc:preprocessor`, `/EHsc`) the package already has to pass either way.
