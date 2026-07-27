# Reflection Code Generation — Handoff

This document lets a fresh session (on a machine with a working `clang` and push
access) continue the reflection-codegen work without re-deriving context. Read it
top to bottom.

## TL;DR status

- **Branch:** `claude/reflection-codegen-automation` (created off
  `claude/reflection-code-generation-gjs58a`).
- **Committed & working:** the entire *infrastructure* — runtime, the new
  `FCLASS` macro, the generator (`tools/generate_reflection.py`), and the xmake
  wiring. Byte-compiles; the C++ is written to compile but **has not been built**
  (this machine has no clang/llvm toolchain and no network to fetch it).
- **Remaining:** migrate the existing ~24 fclass headers/cpps to the new
  `FCLASS()` form and delete their hand-written `_bind_members` (the generator now
  owns them). The tree only builds once **all** are migrated — there is no
  half-migrated buildable state (every `FCLASS` header needs its generated
  `.gen.h`, and any leftover hand-written `_bind_members` collides with the
  generated one).
- **Push is blocked here** with HTTP 403 (the session's git relay only authorizes
  the originally-provisioned branch). Commits are exported as patches in the
  chat; apply them on the target machine and push from there.

## How to resume on the new machine

```bash
git clone <FeatherEngine>
cd FeatherEngine
git checkout claude/reflection-code-generation-gjs58a      # base
git checkout -b claude/reflection-codegen-automation
git am 0001-*.patch 0002-*.patch 0003-*.patch              # the exported commits
# ... do the migration (below) ...
xmake f -m debug && xmake                                   # first build fetches llvm (large)
```

The user approved **migration policy A**: where a member already has a
hand-written *trivial* accessor, delete it and let the generator own it (generated
getters return by value, not `const&`); keep the hand-written accessor and bind it
via `[[get(method)]]`/`[[set(method)]]` only when its body is non-trivial.

## Architecture (what the committed code does)

**`FCLASS` macro** (`core/framework/reflection_macros.h`) — argument-less,
Unreal-style. `FCLASS()`, `FCLASS(singleton)`, `FCLASS(abstract)`,
`FCLASS(explicit_methods)`. It expands to `CURRENT_FILE_ID##_##__LINE__##_GEN_BODY()`,
a per-class macro the generator writes into `<header>.gen.h`. Each `<header>.gen.h`
`#undef`s + `#define`s `CURRENT_FILE_ID`, so **the `<header>.gen.h` include must be
the LAST include of the header** (guarantees `CURRENT_FILE_ID` names the current
file when `FCLASS` expands). When parsing, the generator defines
`FEATHER_REFLECTION_PARSER`, under which `FCLASS(...)` expands to nothing and the
`.gen.h` include is skipped (see the migration include block below).

**Generator** (`tools/generate_reflection.py`) — runs `clang -x c++ -std=c++23
-fsyntax-only -Xclang -ast-dump=json -DFEATHER_REFLECTION_PARSER=1 <includes>
<header>` and parses the JSON with the stdlib. For each class using `FCLASS(`:
recovers name + parent (from the C++ base), extracts fields (name, type, C++
access, `[[attributes]]`), extracts methods, and emits:
- `<header>.gen.h` — the `GEN_BODY` macro: reflection boilerplate (moved out of the
  old macro) + generated inline getters/setters + optional `FDECLARE_SINGLETON` +
  `static void _bind_members();`.
- `register_<sub>_types.gen.{h,cpp}` per core subfolder — the `_bind_members()`
  definitions **and** the `register_<sub>_types()` entry the engine calls (topo-sorted
  `ClassDB::register_class<T>()`; `register_abstract_class<T>()` for `abstract`).

All writes go through `write_if_changed()` — unchanged files keep their mtime.
`.gen.h`/`.gen.cpp` are gitignored (build artifacts), matching the old generator.

**Runtime**
- `ClassInfo` (`core/framework/class_info.h`): `enum AccessLevel {Public,Protected,
  Private}`; `Property` has independent `getter_access`/`setter_access`; `Method`
  has `access`; `ClassInfo` has `is_abstract`/`is_singleton`.
- `ClassDB` (`core/main/class_db.{h,inl}`): access-aware `bind_property`,
  `bind_method`, `bind_static_method`; accessor-based `bind_property_accessors` /
  `bind_property_get` / `bind_property_set`; guarded `*_if_bindable` variants
  (property + method) that compile to a **no-op** when the type/signature isn't
  `VariantCompatible` (so opt-out generation can't break the build); real
  `register_singleton_class`.
- `Variant` (`core/framework/variant.{h,cpp}`): `get`/`set`/`call` now **enforce**
  `AccessLevel::Public`; `get_internal`/`set_internal`/`call_internal` bypass the
  check for engine/editor. `_internal_call` gained a `bool enforce_public`.

**xmake** (`xmake.lua`, `thirdparty/xmake.lua`): `add_requires("llvm",
{kind="binary"})`; `run_codegen` resolves `clang` from the llvm package (falling
back to PATH / the cxx compiler if clang-based) and drives
`generate_reflection.py`; `-Wno-attributes -Wno-unknown-attributes` / `/wd5030`
silence the bare-attribute warnings.

## Attribute rules (the contract for the migration)

Member `_foo` → property `foo` (strip one leading `_`); accessors `get_foo`/`set_foo`.
Reserved access keywords: `public`, `protected`, `private`.

Properties:
1. `[[ignore]]` → not reflected, no accessors.
2. No property attribute → generate **both** accessors with the **member's** access.
3. `[[get]]`/`[[set]]` present → generate only those named (`[[get]]` alone = read-only).
4. `[[get(public|protected|private)]]` → generate with that access; bare `[[get]]` uses
   the member's access.
5. `[[get(MethodName)]]` (arg not an access keyword) → bind that existing method, generate
   nothing. Same for `set`.
6. `[[name(foo)]]` → override the reflected property name (e.g. `_cached_path` → `"path"`).

Methods (default opt-out; `FCLASS(explicit_methods)` = opt-in):
- Public instance methods auto-bind under their C++ name; `[[ignore]]` excludes.
- `[[method]]` force-binds (only way to bind non-public / in opt-in mode / statics).
  `[[method(name)]]` binds under a custom reflected name.
- **Static** methods bind only when annotated `[[method]]` → `bind_static_method`.
- Overloaded/templated names are skipped (ambiguous `&T::name`) — bind by hand.

## Migration recipe (per fclass header + its cpp)

Header:
1. `FCLASS(Name, Parent)` → `FCLASS()`; `FCLASS_SINGLETON(Name, Parent)` →
   `FCLASS(singleton)`.
2. Delete the hand-written `static void _bind_members();` declaration.
3. Add the generated-body include as the **last** include (before `namespace feather {`):
   ```cpp
   #ifndef FEATHER_REFLECTION_PARSER
   #include "<stem>.gen.h"
   #endif
   ```
4. Annotate members/methods only where needed (below).

Cpp:
5. Delete the hand-written `void Name::_bind_members() { ... }` definition(s). Keep
   everything else in the file.

### Per-class notes (what the old `_bind_members` bound — preserve it)

- `resource.{h,cpp}`: `_cached_path` bound as `"path"` → add `[[name(path)]]`.
  `get_rid` is public → auto-bound (no annotation).
- `texture.{h,cpp}`: `_width`, `_height` were bound (public via old bind_property). If
  those members are not in a `public:` block, add `[[get(public), set(public)]]` to keep
  them script-visible.
- `renderer.{h,cpp}`: `_render_scene` bound as a method → if it isn't public, annotate
  `[[method]]`.
- `mesh.{h,cpp}`: `ComplexMesh` is declared **twice** (conditional `#if` around lines
  35/53) — make sure only the compiled definition carries `FCLASS()`, or the generator
  will see a duplicate class. `ComplexMesh` binds `add_indices/add_vertices/get_indices/
  get_vertices` → if public, auto-bound; else `[[method]]` each.
- `rendering_world_feature.{h,cpp}`: static `_load_module` bound as `"_import_module"`
  → `[[method(_import_module)]]` on the static method. (The ECS import loop in
  `world_sim.cpp` constructor calls `get_static_method(child, "_import_module")` — this
  MUST stay bound under that exact name.)
- `vex_renderer.{h,cpp}` (module): `_use_reverse_z` bound as `"use_reverse_z"` → matches
  default de-underscore; keep public visibility.
- `material.{h,cpp}` — the collision-heavy one:
  - `PBRMaterial` factors (`_metallic_factor`, `_roughness_factor`, `_base_color_factor`,
    `_emissive_factor`, `_alpha_blend`, `_double_sided`) already have trivial hand-written
    `get_/set_` and were bound public. **Policy A:** delete the hand-written accessors and
    let generation own them; add `[[get(public), set(public)]]` to each (the members sit in
    a `protected:` block, so without this they'd become protected/non-script-visible).
  - `_base_color_texture` / `_metallic_roughness_texture` / `_normal_texture` /
    `_emissive_texture` are `shared_ptr` (non-marshalable) and have hand-written accessors.
    Simplest: `[[ignore]]` them (keep the hand accessors) — they aren't scalar inspector
    properties. (Alternatively delete the hand accessors and let generation emit them; the
    guarded bind will skip binding but still generate the methods.)
  - `Material::_shader` (shared_ptr, has `get_shader()`; `ShaderMaterial` has `set_shader`)
    → `[[ignore]]` and keep the hand accessors.
- The many loader classes (`*_format_loader.h`, `resource_loader.h`, `extension*.h`) and
  world features had empty or near-empty `_bind_members` → just apply steps 1–3/5, no
  attributes needed. `resource_loader.h` and the singletons in `core/main` use
  `FCLASS(singleton)`.

### Singletons

`FCLASS(singleton)` makes the generated body emit `FDECLARE_SINGLETON(Name)`. Keep the
existing `FSINGLETON_INSTANCE(Name)` in the cpp and the `FSINGLETON_CONSTRUCT_INSTANCE()`
call in the constructor (still hand-written — the generator can't inject a ctor call).
Singletons: `WorldSim`, `ProjectSettings`, `ResourceLoader` (and `ClassDB` uses
`FDECLARE_SINGLETON` directly and is NOT an fclass — leave it).

## Verification (on the clang machine)

1. `python3 tools/generate_reflection.py --core-path core --project-root . --clang $(which clang) -I. -Icore`
   (add `-I` for SimpleMath / package headers if parse errors appear). Inspect a couple of
   generated `core/resources/material.gen.h` and `register_resources_types.gen.cpp`.
2. Run it **twice** → second run prints "0 file(s) updated" (write-if-changed holds).
3. `xmake f -m debug && xmake` (first build fetches the `llvm` package — large).
4. Run `build/bin/feather.standalone`; `ClassDB::print_db()` (BETA) lists classes with
   properties/methods; check that a public property reads via `Variant::get`, a `protected`
   one is denied via `get` but returned via `get_internal`, and `_import_module` is callable.
5. `xmake` again with no changes → no recompilation.

## Known risks / things to check first

- **clang JSON schema assumptions** in `generate_reflection.py`: member `access` is read
  from each node's `"access"` field with an `AccessSpecDecl` fallback; file scoping uses
  `loc.file`/`range.begin.file`; method const/param info parsed from `type.qualType`;
  attributes are text-scanned from the member's source range via `loc.offset`. If a clang
  version differs, adjust `collect_records` / `_handle_field` / `_handle_method`. Verify
  against the actual JSON with `clang -Xclang -ast-dump=json ... | less` on one header.
- **Include resolution for codegen**: `collect_codegen_includes` in `xmake.lua` gathers
  package include dirs; if headers fail to parse, add the missing `-I` there. Parse errors
  are per-header non-fatal (printed as `[WARN]`) but mean that class won't be reflected.
- **`llvm` package** pull is heavy; if undesirable, an alternative is to use the build's own
  clang when the toolchain is clang, or a system clang (`resolve_clang` already falls back).

## Plan file

The full design/plan lives at (session-local)
`~/.claude/plans/create-another-branch-from-adaptive-wave.md`; its content is reproduced by
this handoff. The key sections there mirror this doc.
