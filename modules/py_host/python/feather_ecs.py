"""Define ECS components and systems from a Feather script.

The engine registers what you declare here into the same world its own C++
components live in, so a system you write in Python can query ``Transform`` as
readily as a component you just defined.

    import feather_ecs

    @feather_ecs.component
    class Velocity:
        dx: float
        dy: float

    @feather_ecs.system("Velocity", phase="on_update")
    def drift(entity, velocity, dt):
        velocity.dx += 1.0

Field types are taken from the annotations. ``bool``, ``int`` and ``float`` map
to themselves; ``vec2``, ``vec3`` and ``color`` are spelled as those strings and
arrive in callbacks as plain tuples.
"""

import _feather_ecs

__all__ = [
    "component",
    "system",
    "define_component",
    "define_system",
    "create_entity",
    "add_component",
    "component_view",
    "spawn",
]

# Annotation -> the type name the engine knows. Strings are accepted as-is so a
# script can say `pos: "vec3"` without importing anything for it.
_TYPES = {
    bool: "bool",
    int: "int",
    float: "float",
}

_VALID = {"bool", "int", "float", "vec2", "vec3", "color"}


def _type_name(annotation, field, owner):
    if isinstance(annotation, str):
        if annotation not in _VALID:
            raise TypeError(
                f"{owner}.{field}: unknown field type {annotation!r} "
                f"(expected one of {', '.join(sorted(_VALID))})"
            )
        return annotation

    try:
        return _TYPES[annotation]
    except (KeyError, TypeError):
        raise TypeError(
            f"{owner}.{field}: {annotation!r} is not a field type Feather can store "
            f"(use bool, int, float, or the strings 'vec2', 'vec3', 'color')"
        ) from None


def component(cls):
    """Register the annotated class as an ECS component type.

    The class itself is returned unchanged and is never instantiated: it exists
    to describe a layout. Systems receive a view onto the entity's actual
    storage, not an instance of this class.
    """
    annotations = getattr(cls, "__annotations__", {})
    if not annotations:
        raise TypeError(
            f"{cls.__name__} declares no fields; a component needs at least one "
            f"(annotate them, e.g. `hp: int`)"
        )

    fields = [(name, _type_name(ann, name, cls.__name__)) for name, ann in annotations.items()]
    cls.__feather_component__ = _feather_ecs.define_component(cls.__name__, fields)
    return cls


def system(*components, phase="on_update", name=None):
    """Register the decorated function as a system over the named components.

    Components are named by their class (as returned by ``@component``) or by
    name, which is how a C++ component such as ``Transform`` is reached. The
    function is called once per matching entity, per frame, as
    ``fn(entity, *component_views, dt)``.
    """
    if not components:
        raise TypeError("a system must name at least one component")

    names = [c if isinstance(c, str) else c.__name__ for c in components]

    def decorate(fn):
        _feather_ecs.define_system(
            name or fn.__name__,
            names,
            phase,
            lambda entity, views, dt: fn(entity, *views, dt),
        )
        return fn

    return decorate


def spawn(*components, name=""):
    """Create an entity carrying the named components, and return its id.

    Components are named by class or by name, the same way ``system`` names
    them. Fields start zero-initialized; use ``component_view`` to set them.
    """
    entity = _feather_ecs.create_entity(name)
    for c in components:
        _feather_ecs.add_component(entity, c if isinstance(c, str) else c.__name__)
    return entity


# The raw calls, for anything the decorators do not cover.
define_component = _feather_ecs.define_component
define_system = _feather_ecs.define_system
create_entity = _feather_ecs.create_entity
add_component = _feather_ecs.add_component
component_view = _feather_ecs.component_view
