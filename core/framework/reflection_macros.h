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

// Token-paste helpers. Two levels so macro arguments (e.g. __LINE__ and
// CURRENT_FILE_ID) are expanded before being pasted together.
#define FEATHER_JOIN_INNER(a, b) a##b
#define FEATHER_JOIN(a, b) FEATHER_JOIN_INNER(a, b)

// FCLASS marks a reflected class. Unlike before, it does NOT take the class name
// or parent — the reflection generator (tools/generate_reflection.py) recovers
// them from the C++ declaration. It accepts optional modifiers that the
// generator reads from source:
//
//   FCLASS()                  plain reflected class
//   FCLASS(singleton)         also emits the singleton boilerplate + registers
//                             the class as a singleton
//   FCLASS(abstract)          force registration as abstract (usually redundant:
//                             abstract/non-default-constructible classes are
//                             detected automatically in ClassDB::register_class)
//   FCLASS(novtable)          reflect WITHOUT any virtual function (no vtable
//                             pointer) — see FSTRUCT below, this is the same
//                             thing spelled for a `class`. Every class in a
//                             local inheritance chain must agree on this (the
//                             generator errors otherwise); properties still
//                             work, but a non-static [[method]] cannot bind
//                             (ClassDB::bind_method needs a Reflected* to
//                             dispatch through object_cast, which a vtable-
//                             free type has no way to support — bind a static
//                             method instead).
//
// Both properties and methods are opt-in (mirrors Unreal's UPROPERTY/UFUNCTION):
// a data member reflects only when annotated with [[get]]/[[set]]/[[name(...)]],
// and a method binds only when annotated [[method]] (or [[method(name)]] to bind
// under a custom reflected name). Unannotated members/methods are not reflected.
//
// Mechanism (mirrors Unreal's GENERATED_BODY): FCLASS expands to a per-class
// body macro that the generator writes into "<header>.gen.h". Each generated
// header (re)defines CURRENT_FILE_ID to a token unique to that header, so the
// "<header>.gen.h" include MUST be the LAST include of the header — that
// guarantees CURRENT_FILE_ID names the current file at the point FCLASS expands.
//
// FSTRUCT(...) is FCLASS(...) with `novtable` implied — the natural spelling on
// a `struct` that just wants reflected properties (e.g. an ECS component) and
// none of the polymorphism baggage. It takes the same modifiers as FCLASS
// (though `novtable` is redundant on it) and expands identically; only the
// generator treats it differently. It does NOT need to inherit Reflected, and
// typically inherits nothing at all — reflection here is purely properties
// (+ optional static methods) registered through ClassDB::register_value_class,
// with no factory and no polymorphic is_of_type/get_class_name.
#ifdef FEATHER_REFLECTION_PARSER
// The generator parses headers with this defined so member/method extraction
// never depends on generated output that may not exist yet.
#define FCLASS(...)
#define FSTRUCT(...)
#else
#define FCLASS(...) FEATHER_JOIN(CURRENT_FILE_ID, FEATHER_JOIN(_, FEATHER_JOIN(__LINE__, _GEN_BODY)))()
#define FSTRUCT(...) FEATHER_JOIN(CURRENT_FILE_ID, FEATHER_JOIN(_, FEATHER_JOIN(__LINE__, _GEN_BODY)))()
#endif
