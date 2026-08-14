# Plugin ABI

FeatherEngine loads project code as a dynamic library found by `ResourceLoader::index_project()`.
This document is the design record for that boundary — a GDExtension-style C ABI — so the
rationale lives in the repo instead of leaking into header comments. See git history on the
`plugin-api` branch for staging.

## Why not C++ across the boundary

The previous attempt (`plugin-abi-rework`, abandoned) exported the engine's real C++ classes via
visibility macros. It failed for reasons specific to that shape, not to C++ tooling in general:

- With no private header directory, every declaration became an ABI decision — 88 `FEATHER_API`
  annotations across 38 headers, only the reflected subset machine-checked.
- A hand-written flecs wrapper became a second, permanently-diverging ECS API alongside the
  engine's own use of flecs directly.
- Nothing detected an actual ABI mismatch except a hash of `sizeof(std::string)` and
  `_ITERATOR_DEBUG_LEVEL` — a symptom check, not a contract.
- The consumer is a separate xmake project; every compiler flag and runtime setting had to be
  hand-mirrored across two repos with nothing enforcing agreement.

A pure C boundary removes the *category* of bug: no STL type, no vtable layout, and no
allocator ever crosses it, so CRT/stdlib mismatch stops being something to detect and becomes
something that cannot happen.

## Shape

One pure-C header, `core/extension/feather_interface.h` (`stdint.h`/`stddef.h` only). Engine
functions are resolved **by name** at load time (`FeatherGetProcAddress`), not through a struct
of function pointers — adding an engine function never changes a struct layout, so the ABI
version essentially never needs to bump. The plugin exports exactly one symbol,
`feather_extension_init`, and registers `initialize`/`deinitialize` callbacks per
initialization level (`FEATHER_INIT_CORE` during `index_project()`, `FEATHER_INIT_WORLD` once
`WorldSim` has a live flecs world). Teardown runs levels in reverse.

`Variant` crosses as an opaque, engine-owned pointer — never a struct the plugin lays out
itself, since its size depends on the C++ stdlib in use. The common path (typed method calls)
never touches Variant at all: `ClassInfo::Method::callable` is already
`std::function<Variant(std::span<Variant>)>`, so a `method_ptrcall(method, obj, args, ret)`
shim builds the `Variant` array on the engine's own stack from raw typed pointers, and a
generated binding on the plugin side never allocates one.

Plugin classes register through `classdb_register_extension_class`, an engine-side bridge class
derived from the extensible base (currently just `ResourceFormatLoader`) that forwards its
virtuals to plugin function pointers resolved once at registration. Adding a new extensible base
is a deliberate, bounded act — an allowlist of bridges — not another header to annotate.

Every engine class is otherwise reached through **generated** bindings built from a machine
-readable API dump (`--dump-api`), so growing the surface costs no hand-written ABI code.

## What this removes

`-rdynamic` and symbol interposition for engine singletons, the whole-tree include path
consumers currently get, cross-heap `new`/`delete` of plugin-allocated engine objects, and the
Windows `.def`/import-library requirement (a plugin that only *receives* function pointers needs
no import library). None of these were the actual goal — they were load-bearing only because
the previous boundary compiled engine headers into the plugin.

## Out of scope for this effort

Static shipping mode, Windows verification (the ABI is CRT-agnostic by construction; verifying
that is separate follow-up work), and any change to the reflection code generator
(`tools/codegen/generate_reflection.py` stays Python, stays regex-based — measured at ~32ms for
a full core codegen pass, with libclang costing over 100x that just to parse this tree).
