"""
ecs.py — Flecs ECS modifiers: EcsModule.

A component needs no modifier: it derives from Component (core/world/component.h)
and ClassDB records that parent, which is what World watches to register it.

Unlike core_modifiers.py this isn't part of the base modifier vocabulary in
principle (a headless, non-ECS consumer of this generator wouldn't need it),
but FeatherEngine's own core/world/* uses it, so it ships and is always loaded
the same way core_modifiers.py is. A project that never applies EcsModule is
unaffected: the hooks only run for a class carrying the modifier.
"""

from modifier_api import Modifier


class EcsModuleModifier(Modifier):
    """FCLASS(EcsModule) on an EcsModule subclass. Generates the static
    _import_module(WorldSim*) hook that WorldSim's constructor discovers via
    ClassDB::get_children_names("EcsModule") + get_static_method(child,
    "_import_module") (core/main/world_sim.cpp) — previously hand-written per
    feature (see the old RenderingWorldModule::_load_module)."""
    name = "EcsModule"
    targets = frozenset({"class"})
    value_type = False

    def gen_body_lines(self, cls, ctx):
        return [("protected", "static void _import_module(WorldSim* sim);")]

    def bind_members_lines(self, cls, ctx):
        return [f'ClassDB::bind_static_method(&{cls.name}::_import_module, "_import_module", AccessLevel::Public);']

    def register_cpp_includes(self, cls, ctx):
        # The definition below needs a complete WorldSim, but the class's own
        # header only ever forward-declares it (see ecs_module.h) so that
        # WorldSim doesn't have to be complete in every feature header that
        # just wants to derive from EcsModule. Keeping the #include here,
        # not in gen_body_lines' declaration, is what preserves that.
        return ["main/world_sim.h"]

    def register_cpp_definitions(self, cls, ctx):
        return [
            f"void {cls.name}::_import_module(WorldSim* sim) {{",
            f"\tsim->get_world()->import_module<{cls.name}>();",
            "}",
        ]


MODIFIERS = [EcsModuleModifier()]
