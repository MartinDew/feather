"""
ecs.py — Flecs ECS modifiers: EcsModule, Component, EcsSystem.

Unlike core_modifiers.py these aren't part of the base modifier vocabulary in
principle (a headless, non-ECS consumer of this generator wouldn't need them),
but FeatherEngine's own core/world/* uses them, so they're shipped and always
loaded the same way core_modifiers.py is. A project (or a future core
subsystem) that never applies EcsModule/Component/EcsSystem is unaffected
either way -- these hooks only ever run for something that actually carries
the modifier or attribute (Component's emit_dir is one exception; see its
docstring. EcsSystemModifier's scan_source is another -- it runs
unconditionally per header, looking for [[system(...)]], same reason
Component's emit_dir always runs).
"""

import re

from modifier_api import DirEmission, Modifier, ModifierError


class EcsModuleModifier(Modifier):
    """FCLASS(EcsModule) on an EcsFeature subclass. Generates the static
    _import_module(WorldSim*) hook that WorldSim's constructor discovers via
    ClassDB::get_children_names("EcsFeature") + get_static_method(child,
    "_import_module") (core/main/world_sim.cpp) — previously hand-written per
    feature (see the old RenderingWorldFeature::_load_module).

    _import_module() forwards straight into a hand-written
    `static void {Class}::on_import(ecs::WorldHandle, ecs::EntityHandle)` --
    the class's actual module-setup logic -- rather than going through
    flecs's own world.import<T>()/T(flecs::world) constructor convention.
    That flecs C++ convention is what used to pull ~20 ecs_cpp_*/ecs_fini/
    ecs_stage_* symbols into every EcsModule, including a plugin's (see
    feather-example-project's ExampleModule, which is exactly why this
    changed): world.import<T>() constructs T from a flecs::world by value,
    instantiating flecs's whole C++ template machinery in whatever binary
    that class is compiled into. on_import() needs none of that -- WorldSim's
    ClassDB loop already guarantees each EcsFeature is imported exactly once,
    so flecs's own "already imported" bookkeeping (the actual job
    world.import<T>() does) is redundant here. Engine-internal on_import()
    bodies that still want flecs's builder API (system<>().with<>().up()...)
    get it back in one line via ecs_defs.h's unwrap(WorldHandle) -- see
    rendering_world_feature.cpp."""
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
            f"\t{cls.name}::on_import(sim->ecs_world(), sim->current_scene_handle());",
            "}",
        ]


class ComponentModifier(Modifier):
    """FSTRUCT(Component) (or FCLASS(novtable, Component)) marks a value type
    as a Flecs component. Doesn't touch the class body or _bind_members at
    all -- registration needs a live world, which neither the per-class
    .gen.h macro nor _bind_members()/ClassDB ever has access to. Instead,
    registration is aggregated per source directory into one
    register_<dir>_components(WorldHandle) via emit_dir(), which the engine
    calls once it actually has a world (see register_core_ecs_features).

    Goes through feather::ecs::register_component() (core/world/ecs_api.h)
    rather than flecs's own world.component<T>("T") template: that template
    calls ecs_cpp_component_register/ecs_set_hooks_id, which is exactly what
    would pull flecs into a plugin's undefined-symbol table the moment this
    modifier is applied to a project's own FSTRUCT(Component). register_component()
    takes the same size/alignment/ctor/dtor/copy/move information flecs's
    template would have derived from T via RTTI, but from make_component_desc<T>()
    instead -- ordinary C++ metaprogramming with no flecs dependency -- and the
    NAME comes from the reflected class name already known here at generation
    time, so nothing needs deriving from __PRETTY_FUNCTION__ at runtime either.
    This applies equally to the engine's own core/world/components/*.h (Scene,
    Light, Transform, ...): they go through this exact same emit_dir() pass,
    so converting this one modifier converts them for free."""
    name = "Component"
    targets = frozenset({"class"})
    value_type = True

    def emit_dir(self, classes, ctx):
        members = [c for c in classes if any(m.name == self.name for m, _ in c.resolved_modifiers)]
        func = f"register_{ctx.dir_name}_components"
        cpp_lines = [f"void {func}(ecs::WorldHandle world) {{"]
        cpp_lines += [f'\tecs::register_component(world, ecs::make_component_desc<{c.name}>("{c.name}"));'
                      for c in members]
        cpp_lines += ["}", ""]
        return DirEmission(
            header_includes=["world/ecs_api.h"],
            header_decls=[f"void {func}(ecs::WorldHandle world);", ""],
            cpp_includes=["world/ecs_api.h"],
            cpp_lines=cpp_lines,
        )


# [[system(Phase)]]\n(void )name(params)(; or {) -- declaration-only ("; ",
# body defined elsewhere, the normal C++ free-function pattern) or an inline
# definition (the opening '{'; this modifier only needs the SIGNATURE, so it
# doesn't matter which -- an inline definition is the caller's own ODR
# responsibility, same as any other inline free function in this codebase,
# e.g. rendering_world_feature.cpp's _begin_render_scene). params has no
# nested-paren support ([^()]*) -- a component reference parameter never
# needs one; a default-argument expression with its own call would break
# this, which is an accepted v1 limitation.
_SYSTEM_FUNC_RE = re.compile(
    r"\[\[\s*system\s*\(\s*(\w+)\s*\)\s*\]\]\s*"
    r"(?:void\s+)(\w+)\s*\(([^()]*)\)\s*(?:;|\{)"
)
# "Type& name" or "const Type& name" -- a component this system reads/writes.
_REF_PARAM_RE = re.compile(r"^(?:const\s+)?(\w+)\s*&\s*\w*$")
# The required trailing parameter, bound to TableIter::delta_time.
_DT_PARAM_RE = re.compile(r"^float\s+\w+$")


class EcsSystemModifier(Modifier):
    """[[system(Phase)]] on a free function marks it as a Flecs system:

        [[system(OnUpdate)]]
        void spin(Spinner& s, Transform& t, float dt);

    Unlike EcsModule/Component this isn't a class-body modifier at all --
    there is no FCLASS/FSTRUCT involved, and none of the class-scanning
    machinery (find_class_bodies/build_class) ever sees these declarations,
    since they're plain namespace-scope functions. scan_source() -- a
    Modifier hook that process_header() calls BEFORE its FCLASS/FSTRUCT
    early-return, so a header containing only [[system]] functions and no
    reflected classes still gets scanned -- finds them directly in each
    header's raw text.

    The component list comes from the function's OWN parameter list, not
    from a base class or template argument the way an earlier design here
    tried: every parameter but the last must be "(const) Type&" (a
    component this system reads or writes, in the exact order the generated
    trampoline will unpack TableIter::columns); the last must be
    "float <name>", bound to the per-table delta time
    (TableIter::delta_time). This is the one place in this generator that
    extracts a full parameter list rather than just a name -- see
    generate_reflection.py's MethodPlan, which still only ever records
    name/access/is_static for a class method.

    Registration is aggregated per directory into one
    register_<dir>_systems(WorldHandle) via emit_dir(), exactly like
    ComponentModifier -- see core/world/ecs_api.h's TableIter for what the
    generated per-table trampoline does with each parameter. Unlike
    ComponentModifier, emit_dir() returns None (no DirEmission at all) when a
    directory has no [[system]] functions -- nothing calls
    register_<dir>_systems() unconditionally the way register_core_features.cpp
    calls register_<dir>_components() for every core dir, so emitting an
    always-empty, always-unreferenced function would just be dead code."""
    name = "EcsSystem"

    _PHASES = {"PreUpdate", "OnUpdate", "PostUpdate", "PreStore", "OnStore"}

    def __init__(self):
        # Accumulated across every header in this directory by scan_source(),
        # read back by emit_dir(). Safe as instance state -- see
        # Modifier.scan_source's docstring: build_registry() creates a fresh
        # Modifier instance (by re-exec'ing this file) for every directory
        # pass, so nothing here leaks across directories.
        self._systems = []  # list[(phase, func_name, [component_type, ...], header)]

    def scan_source(self, text, header, ctx):
        for m in _SYSTEM_FUNC_RE.finditer(text):
            phase, name, params_text = m.group(1), m.group(2), m.group(3)
            line = text.count("\n", 0, m.start()) + 1

            if phase not in self._PHASES:
                raise ModifierError(
                    f"{header}:{line}: [[system({phase})]] on '{name}': unknown phase "
                    f"(expected one of {sorted(self._PHASES)})")

            parts = [p.strip() for p in params_text.split(",") if p.strip()]
            if not parts or not _DT_PARAM_RE.match(parts[-1]):
                got = repr(parts[-1]) if parts else "(none)"
                raise ModifierError(
                    f"{header}:{line}: [[system]] function '{name}': last parameter must be "
                    f"'float <name>' (the per-table delta time), got {got}")

            comps = []
            for p in parts[:-1]:
                pm = _REF_PARAM_RE.match(p)
                if not pm:
                    raise ModifierError(
                        f"{header}:{line}: [[system]] function '{name}': parameter {p!r} must be "
                        f"'Type&' or 'const Type&' (a component reference)")
                comps.append(pm.group(1))

            # header is needed at emit_dir() time so the generated .cpp can
            # #include it -- unlike a class (whose own header
            # generate_register_cpp() already includes automatically), a
            # [[system]] function's declaration never goes through the class
            # pipeline, so nothing else would pull its header in.
            self._systems.append((phase, name, comps, header))

    def emit_dir(self, classes, ctx):
        if not self._systems:
            return None

        func = f"register_{ctx.dir_name}_systems"
        cpp_lines = []

        # Each system's own header (declaring the function itself) --
        # deduplicated, since several systems commonly share one header
        # (e.g. this project's systems.h). Unlike a reflected class, whose
        # own header generate_register_cpp() already includes automatically,
        # a [[system]] function never goes through the class pipeline, so
        # nothing else pulls this in.
        cpp_includes = ["world/ecs_api.h"]
        seen_headers = set()
        for _phase, _name, _comps, header in self._systems:
            rel = ctx.relative_include(header)
            if rel not in seen_headers:
                seen_headers.add(rel)
                cpp_includes.append(rel)

        # Trampolines first purely for generated-file readability (top to
        # bottom); nothing here requires forward declarations since both
        # halves land in the same .cpp.
        for _phase, name, comps, _header in self._systems:
            cpp_lines.append(f"static void _{name}_trampoline(ecs::TableIter* it) {{")
            for i, comp in enumerate(comps):
                cpp_lines.append(f"\tauto* _col{i} = static_cast<{comp}*>(it->columns[{i}]);")
            cpp_lines.append("\tfor (int32_t _row = 0; _row < it->count; ++_row) {")
            args = ", ".join(f"_col{i}[_row]" for i in range(len(comps)))
            cpp_lines.append(f"\t\t{name}({args}, it->delta_time);")
            cpp_lines.append("\t}")
            cpp_lines.append("}")
            cpp_lines.append("")

        cpp_lines.append(f"void {func}(ecs::WorldHandle world) {{")
        for phase, name, comps, _header in self._systems:
            cpp_lines.append("\t{")
            cpp_lines.append("\t\tecs::ComponentId terms[] = {")
            for comp in comps:
                cpp_lines.append(f'\t\t\tecs::lookup_component(world, "{comp}"),')
            cpp_lines.append("\t\t};")
            cpp_lines.append(
                f'\t\tecs::SystemDesc desc {{ "{name}", ecs::SystemPhase::{phase}, '
                f"terms, {len(comps)}, &_{name}_trampoline }};"
            )
            cpp_lines.append("\t\tecs::register_system(world, desc);")
            cpp_lines.append("\t}")
        cpp_lines.append("}")
        cpp_lines.append("")

        return DirEmission(
            header_includes=["world/ecs_api.h"],
            header_decls=[f"void {func}(ecs::WorldHandle world);", ""],
            cpp_includes=cpp_includes,
            cpp_lines=cpp_lines,
        )


MODIFIERS = [EcsModuleModifier(), ComponentModifier(), EcsSystemModifier()]
