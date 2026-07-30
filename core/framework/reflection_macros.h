#pragma once

#include "main/class_db.inl"
#include "static_string.hpp"

template <class T>
concept has_is_type_method = requires { T::is_type(); };

template <has_is_type_method T>
static bool get_is_type(StaticString type_name) {
	T::is_of_type(type_name);
}

template <class T>
	requires(!has_is_type_method<T>)
static bool get_is_type(StaticString type_name) {
	return false;
}

struct NO_PARENT {};

// Token-paste helpers (two levels so args like __LINE__/CURRENT_FILE_ID expand
// before being pasted).
#define FEATHER_JOIN_INNER(a, b) a##b
#define FEATHER_JOIN(a, b) FEATHER_JOIN_INNER(a, b)

// Marks a reflected class; the generator (tools/codegen/generate_reflection.py)
// recovers name/parent from the declaration itself. Optional modifiers:
// singleton, abstract, novtable (no vtable -- see FSTRUCT; a non-static
// [[method]] can't bind on such a type since bind_method dispatches through a
// Reflected* via object_cast, so bind a static method instead). Properties and
// methods are opt-in via [[get]]/[[set]]/[[name(...)]] / [[method]] attributes.
//
// Expands to a per-class body macro the generator writes into "<header>.gen.h";
// that include must be the LAST include in the header, since each generated
// header redefines CURRENT_FILE_ID and FCLASS needs it to name the right file.
//
// FSTRUCT is FCLASS with `novtable` implied -- for a plain-property type (e.g.
// an ECS component) with no polymorphism, factory, or Reflected base.
#ifdef FEATHER_REFLECTION_PARSER
// Generator parses with this defined so extraction never depends on
// possibly-stale generated output.
#define FCLASS(...)
#define FSTRUCT(...)
#else
#define FCLASS(...) FEATHER_JOIN(CURRENT_FILE_ID, FEATHER_JOIN(_, FEATHER_JOIN(__LINE__, _GEN_BODY)))()
#define FSTRUCT(...) FEATHER_JOIN(CURRENT_FILE_ID, FEATHER_JOIN(_, FEATHER_JOIN(__LINE__, _GEN_BODY)))()
#endif
