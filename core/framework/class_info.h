#pragma once

#include "callable.h"
#include "static_string.hpp"
#include "variant.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace feather {

// Reflected accessibility of a member. Mirrors the C++ access of the underlying
// member (or the accessibility requested through a property attribute). Used by
// the reflection layer to gate script/editor access (see Variant::get/set/call).
enum class AccessLevel : uint8_t {
	Public = 0,
	Protected,
	Private,
};

struct ClassInfo {
	StaticString name = ""_ss;
	StaticString parent = ""_ss;
	std::vector<const ClassInfo*> children;

	// Whether the class can be instantiated through object_create_func. Abstract
	// and singleton classes have a null factory.
	bool is_abstract = false;
	bool is_singleton = false;
	// True for a value type (FSTRUCT / FCLASS(novtable)): registered through
	// ClassDB::register_value_class, no Reflected base, no factory. Consumers
	// of Property::getter/setter must treat the void* they're handed as a raw
	// T*, never a Reflected* -- there is no vtable to cast through.
	bool is_value_type = false;

	struct Property {
		StaticString name;
		// variant type to convert to
		VariantType type;

		// Accessibility of each accessor, tracked independently so a property can
		// be e.g. publicly readable but privately writable (C#-style). The
		// script-facing Variant::get/set only reach Public accessors; the
		// privileged *_internal path bypasses these.
		AccessLevel getter_access = AccessLevel::Public;
		AccessLevel setter_access = AccessLevel::Public;

		// Function pointers for get/set
		std::function<Variant(void*)> getter; // Takes object pointer, returns Variant
		std::function<void(void*, Variant)> setter; // Takes object pointer and value
	};
	std::vector<Property> properties;

	struct Method {
		StaticString name;
		// Possibly need to store param names later
		AccessLevel access = AccessLevel::Public;
		Callable callable;
	};

	std::vector<Method> methods;

	std::function<Variant()> object_create_func;
};

} //namespace feather
