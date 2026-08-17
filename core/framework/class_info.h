#pragma once

#include "callable.h"
#include "static_string.hpp"
#include "variant.h"

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace feather {

// Mirrors the C++ access of a member (or an explicit property attribute); gates
// script/editor access via Variant::get/set/call.
enum class AccessLevel : uint8_t {
	Public = 0,
	Protected,
	Private,
};

struct ClassInfo {
	StaticString name = ""_ss;
	StaticString parent = ""_ss;
	std::vector<const ClassInfo*> children;

	// Abstract/singleton classes have a null object_create_func.
	bool is_abstract = false;
	bool is_singleton = false;
	// Value type (FSTRUCT / FCLASS(novtable)): no Reflected base or factory --
	// Property::getter/setter's void* is a raw T*, not a Reflected*.
	bool is_value_type = false;
	// Whether classdb_register_extension_class (the plugin ABI) accepts this
	// class as a parent -- set by the engine-side bridge registration for that
	// base (core/extension/bridges), not by generate_reflection.py. A plain
	// reflected class defaults to false: most classes have no plugin-callable
	// virtuals and no bridge to forward them through.
	bool is_extensible = false;

	// sizeof/alignof T, captured at registration time. Backs the plugin ABI's
	// classdb_class_get_size/_get_align and lets a value type's ECS component
	// registration (ecs_register_component needs sizeof/alignof) come from the
	// engine's own layout instead of the plugin guessing it.
	uint32_t size = 0;
	uint32_t align = 0;

	struct Property {
		StaticString name = ""_ss;
		// variant type to convert to
		VariantType type;
		// "" unless type == OBJECT (the pointee's class name) or the property is
		// really an enum stored as INT (the enum's name) -- see
		// ClassDB::bind_enum_property_accessors.
		StaticString type_class = ""_ss;

		// Tracked independently so a property can be e.g. publicly readable but
		// privately writable; Variant::get/set only reach Public, *_internal bypasses this.
		AccessLevel getter_access = AccessLevel::Public;
		AccessLevel setter_access = AccessLevel::Public;

		// Function pointers for get/set
		std::function<Variant(void*)> getter; // Takes object pointer, returns Variant
		std::function<void(void*, Variant)> setter; // Takes object pointer and value
	};
	std::vector<Property> properties;

	// Physical layout of a value type: every data member, not just ones with a
	// [[get]]/[[set]] accessor -- Field is not Property. Transform::rotation
	// (a Quaternion) has no VariantType mapping and so is a Field but never a
	// Property; a plugin-side mirror still needs its offset to reproduce the
	// struct's layout. Populated for value types only: a Reflected-derived
	// class carries a vtable pointer and implementation details a plugin has
	// no business mirroring byte-for-byte.
	struct Field {
		StaticString name = ""_ss;
		VariantType type;
		StaticString type_class = ""_ss; // enum name, when applicable; else ""
		uint32_t offset = 0;
		uint32_t size = 0;
	};
	std::vector<Field> fields;

	struct Method {
		StaticString name = ""_ss;
		AccessLevel access = AccessLevel::Public;
		Callable callable;
		// Captured at bind time from the method's real signature -- Callable itself
		// only knows its arity, not the types, so this is the sole source for a
		// generated (non-Variant) binding to know what to marshal.
		VariantType return_type = VariantType::NIL;
		StaticString return_class = ""_ss; // "" unless return_type == OBJECT
		std::vector<VariantType> param_types;
		std::vector<StaticString> param_classes; // parallel to param_types; "" unless OBJECT
		// Best-effort, from the declaration text (generate_reflection.py);
		// empty for a hand-written bind_method call that didn't pass any.
		std::vector<StaticString> param_names;
		// bind_method injects a leading Reflected* receiver into the Callable
		// that param_types never sees; the plugin ABI's ptrcall shim needs to
		// know that to build the right argument count.
		bool is_static = false;
		bool is_const = false;
		bool is_virtual = false;
	};

	std::vector<Method> methods;

	std::function<Variant()> object_create_func;
};

// A free (not nested in any ClassInfo) enum reflected alongside a class in the
// same header -- e.g. LightType next to Light. Not tied to a single class:
// nothing here stops an enum from being shared by several properties.
struct EnumInfo {
	StaticString name = ""_ss;
	VariantType underlying = VariantType::INT;
	std::vector<std::pair<StaticString, int64_t>> values;
};

} //namespace feather
