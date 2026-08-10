#pragma once

#include "callable.h"
#include "static_string.hpp"
#include "variant.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace feather {

// Mirrors the C++ access of a member (or an explicit property attribute); gates
// script/editor access via Variant::get/set/call.
enum class AccessLevel : uint8_t {
	Public = 0,
	Protected,
	Private,
};

struct FEATHER_API ClassInfo {
	StaticString name = ""_ss;
	StaticString parent = ""_ss;
	std::vector<const ClassInfo*> children;

	// Abstract/singleton classes have a null object_create_func.
	bool is_abstract = false;
	bool is_singleton = false;
	// Value type (FSTRUCT / FCLASS(novtable)): no Reflected base or factory --
	// Property::getter/setter's void* is a raw T*, not a Reflected*.
	bool is_value_type = false;

	struct Property {
		StaticString name;
		// variant type to convert to
		VariantType type;

		// Tracked independently so a property can be e.g. publicly readable but
		// privately writable; Variant::get/set only reach Public, *_internal bypasses this.
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
