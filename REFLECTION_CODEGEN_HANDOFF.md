# Reflection Code Generation — Handoff

This document lets a fresh session continue the reflection-codegen work without
re-deriving context. Read it top to bottom.

## TL;DR status

- **Done:** the infrastructure (runtime, the `FCLASS` macro, the generator
  `tools/generate_reflection.py`, the xmake wiring) and the migration of all
  fclass headers/cpps to the `FCLASS()` form are both complete and committed.
- **Done:** the generator's parsing backend was rewritten from a clang-AST-dump
  backend to a purely syntactic Python-stdlib scanner. This was a deliberate
  follow-up, not part of the original migration — see "Generator" under
  Architecture below for how it works, and "Known risks" for its limits. There
  is no clang/LLVM dependency anywhere in the codegen path anymore, and no
  pip/venv requirement either.

The user approved **migration policy A** during the original `FCLASS()` migration:
where a member already had a hand-written *trivial* accessor, it was deleted and
the generator owns it (generated getters return by value, not `const&`); a
hand-written accessor was kept and bound via `[[get(method)]]`/`[[set(method)]]`
only when its body was non-trivial. This is background for reading the
per-class notes below — the migration itself is finished, this isn't a to-do.

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

**Generator** (`tools/generate_reflection.py`) — a purely syntactic scanner
(Python stdlib `re`, no compiler, no external process). Reflection is opt-in —
a member/method is only ever inspected once it carries a
`[[get]]`/`[[set]]`/`[[name]]`/`[[method]]` attribute — so the tool never needs
to resolve what a type or base class *means*, only what it's spelled as in
source: `blank_comments_and_literals()` neutralizes comments/string literals
(preserving offsets), `find_class_bodies()` locates `class`/`struct`
definitions and their brace-matched body span, `iter_member_chunks()` splits a
body into depth-0 member declarations while tracking the current access level,
and `classify_chunk()` turns a chunk into a field or method (name + verbatim
type spelling for fields; name + static-ness for methods). For each class
whose body contains an `FCLASS(...)` occurrence: recovers name + parent (from
the base-clause text), extracts fields (name, type *exactly as written*, C++
access, `[[attributes]]`), extracts methods, and emits:
- `<header>.gen.h` — the `GEN_BODY` macro: reflection boilerplate (moved out of the
  old macro) + generated inline getters/setters + optional `FDECLARE_SINGLETON` +
  `static void _bind_members();`.
- `register_<sub>_types.gen.{h,cpp}` per core subfolder — the `_bind_members()`
  definitions **and** the `register_<sub>_types()` entry the engine calls (topo-sorted
  `ClassDB::register_class<T>()`; `register_abstract_class<T>()` for `abstract`).

All writes go through `write_if_changed()` — unchanged files keep their mtime.
`.gen.h`/`.gen.cpp` are gitignored (build artifacts), matching the old generator.

**Not a general C++ parser** — it assumes well-formed, not-too-exotic C++:
no raw string literals, no digraphs/trigraphs, base clauses limited to a single
simple (possibly namespace/template-qualified) base, and reflected
declarations that don't differ in shape between preprocessor-conditional
branches. This matches everything actually written in the ~22 `FCLASS`
headers today. Because a chunk's attributes are only ever read from its own
*leading* `[[...]]`, a nested class/struct definition is swallowed whole as
one opaque chunk and never contributes members of its own, even if something
inside it happens to carry a `[[method]]`/`[[get]]` — so there's no need to
special-case "don't recurse into nested types" the way the old clang backend
had to.

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

**xmake** (`xmake.lua`, `xmake/modules/feather_codegen.lua`): `run_codegen`
(attached to `feather.editor`/`feather.standalone`'s `before_build`) just
invokes `python3 tools/generate_reflection.py ...` — no compiler resolution, no
include-dir collection, no `llvm` package. `-Wno-attributes
-Wno-unknown-attributes` / `/wd5030` still silence the bare-attribute warnings
(unrelated to codegen — those quiet the *compiler* about `[[get]]` etc. being
unrecognized attributes in the actual engine build).

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
- `mesh.{h,cpp}`: `ComplexMesh` binds `add_indices/add_vertices/get_indices/
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

## Verification

1. `python3 tools/generate_reflection.py --core-path core --project-root . --module-path modules/vex_renderer`
   — no `--clang`/`-I` flags anymore, nothing to resolve. Inspect a couple of generated
   `core/resources/material.gen.h` and `register_resources_types.gen.cpp`.
2. Run it **twice** → second run prints "0 file(s) updated" (write-if-changed holds).
3. `xmake f -m debug && xmake` — works with no clang/LLVM on the machine at all now.
4. Run `build/bin/feather.standalone`; `ClassDB::print_db()` (BETA) lists classes with
   properties/methods; check that a public property reads via `Variant::get`, a `protected`
   one is denied via `get` but returned via `get_internal`, and `_import_module` is callable.
5. `xmake` again with no changes → no recompilation.

## Known risks / things to check first

- **The scanner is syntactic, not semantic** (see "Not a general C++ parser" above). If a
  new `FCLASS` header uses a shape it can't handle — multiple inheritance, a raw string
  literal, a reflected member whose declaration differs between `#if` branches — the
  generator raises a `ParseError` naming the file (fatal, on purpose: a silently-skipped
  annotated member is exactly the failure mode this rewrite was meant to eliminate; see the
  git history around "Replace the clang AST backend" for the incident that motivated it —
  clang would silently degrade an unresolved type to `int` on a parse failure, which passed
  CI on Linux and broke only on Windows). Fix is almost always to simplify the declaration's
  shape, not to extend the scanner.
- **Attribute detection is leading-only**: `classify_chunk()` only reads a member's *own*
  leading `[[...]]`, immediately before its declarator (on the same line or the line above,
  any number of blank lines apart — anything else in between is not scanned). Don't rely on
  an attribute placed anywhere else.
- **Line numbers matter**: `fclass_line` (the `FCLASS(...)` occurrence's line) feeds the
  `<file_id>_<line>_GEN_BODY` macro name via `__LINE__`, so it must exactly match what the
  C++ preprocessor sees. `process_header()` opens files with `newline=""` specifically to
  keep CRLF checkouts byte-accurate — don't "simplify" that to `read_text()`.
