"""
core_modifiers.py — the FCLASS/FSTRUCT modifiers every reflected type can use,
regardless of ECS or any other higher-level concern: singleton, abstract,
novtable. Always loaded (see build_registry in generate_reflection.py) — these
define the base modifier vocabulary itself, not an opt-in extension a project
chooses to pull in.

singleton/abstract used to be hardcoded directly into build_class()/
generate_gen_header()/generate_register_cpp(); porting them onto the Modifier
API (rather than keeping them as a special case alongside it) is what proves
the API is expressive enough for a project's own modifiers.
"""

from modifier_api import Modifier


class SingletonModifier(Modifier):
    """FCLASS(singleton). Reflection contract with FDECLARE_SINGLETON/
    FSINGLETON_INSTANCE (core/framework/singleton_helpers.h): adds the
    get()/instance boilerplate to the class body and routes registration
    through ClassDB::register_singleton_class, which -- unlike register_class
    -- never installs an object_create_func (the engine constructs the single
    instance itself; reflection only ever resolves it via T::get())."""
    name = "singleton"
    targets = frozenset({"class"})
    value_type = False

    def gen_header_includes(self, cls, ctx):
        return ["framework/singleton_helpers.h"]

    def gen_body_lines(self, cls, ctx):
        return [("pre", f"FDECLARE_SINGLETON({cls.name});")]

    def register_statements(self, cls, ctx):
        return [f"ClassDB::register_singleton_class<{cls.name}>();"]


class AbstractModifier(Modifier):
    """FCLASS(abstract). Usually redundant -- ClassDB::register_class already
    auto-detects an abstract/non-default-constructible T at compile time and
    routes to register_abstract_class itself -- this is the explicit "force
    it" escape hatch for a type that's abstract in spirit but not in the
    is_abstract_v<T>/default-constructible sense the auto-detection checks."""
    name = "abstract"
    targets = frozenset({"class"})
    value_type = False

    def register_statements(self, cls, ctx):
        return [f"ClassDB::register_abstract_class<{cls.name}>();"]


class NovtableModifier(Modifier):
    """FCLASS(novtable) -- the `class`-spelling of what FSTRUCT is for
    `struct`. Purely a marker: ClassDesc.is_value_type is already computed
    directly from this token's presence in build_class(), before modifiers
    are even resolved through the registry (a purely syntactic scanner can't
    otherwise tell whether a base class spelled in another header eventually
    reaches Reflected, so this has to be an explicit, local signal -- see the
    ClassDesc.is_value_type docstring in generate_reflection.py). Registering
    it here only makes "novtable" a *known* token so FCLASS(novtable) doesn't
    trip the "unknown modifier" check; it has no codegen effect of its own."""
    name = "novtable"
    targets = frozenset({"class"})
    value_type = True


MODIFIERS = [SingletonModifier(), AbstractModifier(), NovtableModifier()]
