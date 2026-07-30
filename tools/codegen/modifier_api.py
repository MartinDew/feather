"""
modifier_api.py — extension API for FCLASS(...)/FSTRUCT(...) modifiers.

generate_reflection.py hardcoded exactly two modifiers (singleton, abstract)
directly in its own emission functions. Adding a third meant editing the
generator itself — fine for the engine's own needs, a dead end for a game
project that wants its own cross-cutting FCLASS concern (e.g. an ECS
`Component`/`EcsModule` pair; see tools/codegen/extensions/ecs.py) without
forking the generator.

A Modifier is a plain object with one method per emission site the generator
already has. Each hook mirrors an existing, hand-written piece of generated
code (see generate_reflection.py's generate_gen_header/_bind_members_body/
generate_register_cpp) — a modifier only overrides the hooks it needs;
everything else defaults to "contribute nothing".

Registered modifiers are scoped per source-directory pass (see build_registry
in generate_reflection.py): the engine's own modifiers in tools/codegen/
extensions/ are always loaded, and anything passed via --extension is loaded
only for the directory it was scoped to. This is load-bearing, not cosmetic —
a module or game codegen pass also regenerates core/, and if the loaded
extension set differed between passes, core's generated output would differ
depending on who ran the tool, defeating write_if_changed's whole point.
"""

import importlib.util
import re
from dataclasses import dataclass, field as dc_field
from pathlib import Path

_MODIFIER_TOKEN_RE = re.compile(r"^(\w+)\s*(?:\(\s*(.*?)\s*\))?$", re.DOTALL)


class ModifierError(RuntimeError):
    """Raised by a Modifier hook (or by Registry.resolve for an unknown/
    misused token). The caller (generate_reflection.py) always catches this
    and re-raises as a ParseError with the class/file/line prefixed on -- a
    modifier author never needs to know about that convention."""


@dataclass
class EmitContext:
    """Passed to every Modifier hook.

    dir_name is the source directory currently being processed (e.g. "world"
    for core/world, or a module's own directory name) -- the same name that
    ends up in register_<dir_name>_types.gen.{h,cpp}.

    hoist(code_lines, condition) is only meaningful while a class body is
    being emitted (gen_header_includes/gen_body_lines): it wraps code_lines in
    a helper #define guarded by `#if condition`/`#else`/`#endif` and returns
    the helper's name to reference in place of the lines themselves -- the
    same mechanism generate_gen_header() uses for a #ifdef-guarded property
    accessor (a raw #if can't be spliced into a backslash-continued macro
    body). Calling it outside that context raises ModifierError.
    """
    dir_name: str
    _hoist: object = None

    def hoist(self, code_lines: list, condition: str) -> str:
        if self._hoist is None:
            raise ModifierError(
                "ctx.hoist() is only available while a class body is being emitted "
                "(gen_header_includes/gen_body_lines)"
            )
        return self._hoist(code_lines, condition)

    def error(self, msg: str) -> ModifierError:
        return ModifierError(msg)


@dataclass
class DirEmission:
    """Extra content a modifier wants aggregated over an entire source
    directory, returned from Modifier.emit_dir(). Appended to
    register_<dir>_types.gen.{h,cpp} after the per-class content
    generate_gen_header()/generate_register_cpp() already produce -- see
    e.g. ecs.py's ComponentModifier, which has no per-class _bind_members
    hook to attach to (world.component<T>() needs a live flecs::world&,
    which _bind_members()/ClassDB never has) and instead emits one
    aggregate register_<dir>_components(World&) function."""
    header_includes: list = dc_field(default_factory=list)   # quoted, core-relative
    header_decls: list = dc_field(default_factory=list)      # raw lines, inside `namespace feather { ... }`
    cpp_includes: list = dc_field(default_factory=list)       # quoted, core-relative
    cpp_lines: list = dc_field(default_factory=list)          # raw lines, inside `namespace feather { ... }`


class Modifier:
    """Base class for a modifier plugged into an FCLASS(...)/FSTRUCT(...)
    token list, e.g. `FCLASS(singleton)` or `FSTRUCT(Component)`. Subclass
    and override only the hooks that apply; everything else defaults to
    "contributes nothing" (see the no-op bodies below).

    name        the literal token, e.g. "singleton", "EcsModule".
    targets     which declaration kinds this modifier can be attached to.
                Only {"class"} is consumed by generate_reflection.py today --
                bind_property_lines/bind_method_lines exist as documented
                extension points for a future property/method-level modifier
                syntax, not yet wired to any attribute grammar.
    takes_args  True if `Name(...)` is valid, e.g. a future
                `System(OnUpdate, Position, Velocity)` -- the raw text between
                the parens is passed through verbatim as `arg` to every hook.
                False (the default) means a bare token; `Name(anything)` is a
                ModifierError.
    value_type  None: valid on both value types and Reflected-derived
                classes. True: only valid on a value type (FSTRUCT /
                FCLASS(novtable)). False: only valid on a Reflected-derived
                class. Checked once, centrally, by build_class -- individual
                hooks don't need to re-check it.
    """
    name: str = ""
    targets: frozenset = frozenset({"class"})
    takes_args: bool = False
    value_type = None  # None | True | False

    def validate(self, cls, ctx: EmitContext):
        """Raise ModifierError for anything not already covered by `targets`/
        `value_type` (e.g. Component rejecting a class that also carries some
        other incompatible modifier)."""

    def gen_header_includes(self, cls, ctx: EmitContext) -> list:
        """Extra #include lines (quoted, core-relative, e.g.
        "framework/singleton_helpers.h") for <header>.gen.h. Deduplicated
        across every class in the file by the caller."""
        return []

    def gen_body_lines(self, cls, ctx: EmitContext) -> list:
        """list[(section, line)] to splice into the class body. section is
        one of "pre" (before the reflection boilerplate -- where
        FDECLARE_SINGLETON sits), "public", "protected", "private"."""
        return []

    def bind_members_lines(self, cls, ctx: EmitContext) -> list:
        """Extra statements (no trailing semicolon requirement waived --
        write them exactly as they should appear, semicolon included) for the
        generated T::_bind_members() body."""
        return []

    def register_statements(self, cls, ctx: EmitContext):
        """Statements to call in register_<dir>_types() for this class,
        replacing the default `ClassDB::register_class<T>()` /
        `register_value_class<T>()` choice. Return None to leave the default
        alone (most modifiers should). If more than one applied modifier
        returns non-None, the first one (in resolved/sorted order) wins."""
        return None

    def register_cpp_includes(self, cls, ctx: EmitContext) -> list:
        """Extra #include lines (quoted, core-relative) for
        register_<dir>_types.gen.cpp."""
        return []

    def register_cpp_definitions(self, cls, ctx: EmitContext) -> list:
        """Raw lines defining out-of-line member(s) this modifier declared in
        gen_body_lines (e.g. EcsModule's `T::_import_module` body) -- placed
        in register_<dir>_types.gen.cpp, not the per-header .gen.h, so a
        heavier dependency (e.g. a complete WorldSim) doesn't leak into every
        header that includes the FCLASS'd type."""
        return []

    def emit_dir(self, classes: list, ctx: EmitContext):
        """Called once per source-directory pass (not per class) with every
        class reflected in that directory, applied or not -- the modifier
        itself filters (typically via `any(m.name == self.name for m, _ in
        c.resolved_modifiers)`). Return a DirEmission, or None/falsy to
        contribute nothing."""
        return None

    def bind_property_lines(self, cls, prop, ctx: EmitContext) -> list:
        """Reserved for a future property-level modifier syntax. Not called
        by generate_reflection.py today."""
        return []

    def bind_method_lines(self, cls, method, ctx: EmitContext) -> list:
        """Reserved for a future method-level modifier syntax (e.g. System's
        query/callback). Not called by generate_reflection.py today."""
        return []


class Registry:
    """The modifiers known for one source-directory pass. Built fresh per
    process_source_dir() call (see build_registry in generate_reflection.py)
    -- see the module docstring for why per-dir scoping matters."""

    def __init__(self):
        self._modifiers: dict = {}

    def register(self, modifier: Modifier):
        if not modifier.name:
            raise ModifierError(f"{modifier!r}: a Modifier must set a non-empty `name`")
        if modifier.name in self._modifiers:
            raise ModifierError(f"duplicate modifier registration: '{modifier.name}'")
        self._modifiers[modifier.name] = modifier

    def modifiers(self):
        return list(self._modifiers.values())

    def names(self):
        return sorted(self._modifiers)

    def resolve(self, raw_tokens) -> list:
        """raw_tokens: the class's raw FCLASS(...)/FSTRUCT(...) modifier
        tokens, e.g. {"singleton"} or {"System(OnUpdate, Position)"}. Returns
        list[(Modifier, arg_text_or_None)], sorted by modifier name for
        deterministic emission order regardless of how the tokens were
        written. Raises ModifierError for an unknown token or a bare/argued
        mismatch against `takes_args`."""
        out = []
        for tok in raw_tokens:
            m = _MODIFIER_TOKEN_RE.match(tok)
            if not m:
                raise ModifierError(f"couldn't parse modifier token: {tok!r}")
            name, arg = m.group(1), m.group(2)
            modifier = self._modifiers.get(name)
            if modifier is None:
                known = ", ".join(self.names()) or "<none>"
                raise ModifierError(f"unknown modifier '{name}' (known: {known})")
            if arg is not None and not modifier.takes_args:
                raise ModifierError(f"modifier '{name}' doesn't take arguments, got '{name}({arg})'")
            out.append((modifier, arg))
        out.sort(key=lambda pair: pair[0].name)
        return out


def _load_module_from_path(path: Path):
    spec = importlib.util.spec_from_file_location(path.stem, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_extension_file(registry: Registry, path: Path):
    """An extension file must define a top-level `MODIFIERS` list of Modifier
    instances -- the same convention tools/codegen/extensions/*.py itself
    uses, so an engine built-in and a project extension look identical."""
    mod = _load_module_from_path(path)
    modifiers = getattr(mod, "MODIFIERS", None)
    if modifiers is None:
        raise ModifierError(f"{path}: extension module must define a top-level MODIFIERS list of Modifier instances")
    for modifier in modifiers:
        registry.register(modifier)


def load_extension_path(registry: Registry, path: Path):
    """path may be a single .py file or a directory of them (loaded in sorted
    order for determinism); a directory entry starting with "_" is skipped
    (e.g. a shared "_helpers.py" imported by the others, not itself an
    extension module)."""
    if path.is_dir():
        for f in sorted(path.glob("*.py")):
            if f.name.startswith("_"):
                continue
            load_extension_file(registry, f)
    else:
        load_extension_file(registry, path)
