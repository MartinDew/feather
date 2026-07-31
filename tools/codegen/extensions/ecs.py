"""
ecs.py — Flecs ECS modifiers: EcsModule, Component.

Unlike core_modifiers.py these aren't part of the base modifier vocabulary in
principle (a headless, non-ECS consumer of this generator wouldn't need them),
but FeatherEngine's own core/world/* uses them, so they're shipped and always
loaded the same way core_modifiers.py is. A project (or a future core
subsystem) that never applies EcsModule/Component is unaffected either way --
these hooks only ever run for a class that actually carries the modifier
(Component's emit_dir is the one exception; see its docstring).
"""

from modifier_api import DirEmission, Modifier


class EcsModuleModifier(Modifier):
    """FCLASS(EcsModule) on an EcsFeature subclass. Generates the static
    _import_module(WorldSim*) hook that WorldSim's constructor discovers via
    ClassDB::get_children_names("EcsFeature") + get_static_method(child,
    "_import_module") (core/main/world_sim.cpp) — previously hand-written per
    feature (see the old RenderingWorldFeature::_load_module)."""
    name = "EcsModule"
    targets = frozenset({"class"})
    value_type = False

    def gen_body_lines(self, cls, ctx):
        return [("protected", "static void _import_module(WorldSim* sim);")]

    def bind_members_lines(self, cls, ctx):
        return [f'ClassDB::bind_static_method(&{cls.name}::_import_module, "_import_module", AccessLevel::Public);']

    def register_cpp_includes(self, cls, ctx):
        # The definition below needs a complete WorldSim, but the class's own
        # header only ever forward-declares it (see ecs_feature.h) so that
        # WorldSim doesn't have to be complete in every feature header that
        # just wants to derive from EcsFeature. Keeping the #include here,
        # not in gen_body_lines' declaration, is what preserves that.
        return ["main/world_sim.h"]

    def register_cpp_definitions(self, cls, ctx):
        return [
            f"void {cls.name}::_import_module(WorldSim* sim) {{",
            f"\tsim->get_world()->import<{cls.name}>();",
            "}",
        ]


class ComponentModifier(Modifier):
    """FSTRUCT(Component) (or FCLASS(novtable, Component)) marks a value type
    as a Flecs component. Doesn't touch the class body or _bind_members at
    all -- world.component<T>("T") needs a live flecs::world&, which neither
    the per-class .gen.h macro nor _bind_members()/ClassDB ever has access to.
    Instead, registration is aggregated per source directory into one
    register_<dir>_components(World&) via emit_dir(), which the engine calls
    once it actually has a world (see register_core_ecs_features)."""
    name = "Component"
    targets = frozenset({"class"})
    value_type = True

    def emit_dir(self, classes, ctx):
        members = [c for c in classes if any(m.name == self.name for m, _ in c.resolved_modifiers)]
        func = f"register_{ctx.dir_name}_components"
        cpp_lines = [f"void {func}(World& world) {{"]
        cpp_lines += [f'\tworld.component<{c.name}>("{c.name}");' for c in members]
        cpp_lines += ["}", ""]
        return DirEmission(
            header_includes=["world/ecs_defs.h"],
            header_decls=[f"void {func}(World& world);", ""],
            cpp_includes=["world/ecs_defs.h"],
            cpp_lines=cpp_lines,
        )


MODIFIERS = [EcsModuleModifier(), ComponentModifier()]
