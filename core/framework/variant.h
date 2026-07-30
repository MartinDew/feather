#pragma once

#include "assert.h"
#include "container_utils.h"
#include "path.h"
#include "variant_array.h"

#include "reflected.h"
#include "rendering/mesh_data.h"
#include "resources/rid.h"
#include <math/math_defs.h>

#include <expected>
#include <string>
#include <type_traits>
#include <variant>

namespace feather {

enum class VariantType : uint8_t {
	NIL = 0,
	BOOL,
	// Math
	INT,
	FLOAT,
	VECTOR3,
	VECTOR2,
	VERTEX,
	COLOR,
	// Misc
	RID,
	STRING,
	ARRAY,
	PATH,
	// Objects
	OBJECT,
	// Invalid
	INVALID
};

#define VARIANT_TYPE_OPTION(class_name, variant_name)                                                                  \
	else if constexpr (std::is_same_v<T, class_name>) {                                                                \
		return VariantType::variant_name;                                                                              \
	}

// Helper to get VariantType enum from C++ type
template <class T>
consteval VariantType get_variant_type() {
	if constexpr (std::is_same_v<T, bool>) {
		return VariantType::BOOL;
	}
	else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
		return VariantType::INT;
	}
	else if constexpr (std::is_floating_point_v<T>) {
		return VariantType::FLOAT;
	}
	else if constexpr (std::is_same_v<Vector3, T>) {
		return VariantType::VECTOR3;
	}
	else if constexpr (std::is_same_v<Vector2, T>) {
		return VariantType::VECTOR2;
	}
	else if constexpr (std::is_same_v<Color, T>) {
		return VariantType::COLOR;
	}
	else if constexpr (std::is_same_v<Vertex, T>) {
		return VariantType::VERTEX;
	}
	else if constexpr (std::is_same_v<T, std::string>) {
		return VariantType::STRING;
	}
	else if constexpr (std::is_same_v<T, VariantArray> || std::is_array_v<T> || is_contiguous_container<T>) {
		return VariantType::ARRAY;
	}
	VARIANT_TYPE_OPTION(RID, RID)
	VARIANT_TYPE_OPTION(Path, PATH)
	// object
	else if constexpr ((std::is_pointer_v<T> && std::is_base_of_v<Reflected, std::remove_pointer_t<T>>) ||
					   std::is_base_of_v<Reflected, T>) {
		return VariantType::OBJECT;
	}
	// Void case
	else if constexpr (std::is_same_v<T, std::nullptr_t> || std::is_void_v<T>) {
		return VariantType::NIL;
	}
	// Default case
	else {
		return VariantType::INVALID;
	}
}

// Type trait to check if a type can be converted to Variant
template <typename T>
concept VariantCompatible = get_variant_type<T>() != VariantType::INVALID;

struct ClassInfo;

class Variant {
	using InternalVariant = std::variant<std::monostate,
										 bool,
										 int,
										 real_t,
										 Vector2,
										 Vector3,
										 Vertex,
										 Color,
										 std::string,
										 Path,
										 VariantArray,
										 RID,
										 Reflected*>;

	InternalVariant _data;
	VariantType _type;
	ClassInfo* _object_info = nullptr;

	void set_class_info(StaticString class_name);

	// Shared method-call implementation. When enforce_public is true the call is
	// denied unless the method is publicly accessible; the privileged *_internal
	// entry points pass false to bypass the check.
	[[nodiscard]] Variant
	_internal_call(std::string_view method_name, std::span<Variant> args, bool enforce_public) const;

public:
	// Default constructor - NIL
	Variant() : _data(std::monostate {}), _type(VariantType::NIL) {}

	Variant(Reflected& ref);

	// String literal constructor
	Variant(const char* str) : _data(std::string(str)), _type(VariantType::STRING) {}

	// Generic constructor with concept constraint
	template <VariantCompatible T>
	Variant(T value) {
		constexpr VariantType type = get_variant_type<T>();
		_type = type;

		if constexpr (type == VariantType::BOOL) {
			_data = std::move(value);
		}
		else if constexpr (type == VariantType::INT) {
			_data = static_cast<int>(value);
		}
		else if constexpr (type == VariantType::FLOAT) {
			_data = static_cast<real_t>(value);
		}
		else if constexpr (type == VariantType::VECTOR3) {
			_data = std::move(value);
		}
		else if constexpr (type == VariantType::STRING) {
			_data = std::move(value);
		}
		else if constexpr (type == VariantType::ARRAY) {
			if constexpr (std::is_same_v<T, VariantArray>)
				_data = std::move(value);
			else {
				_data = VariantArray { value.begin(), value.end() };
			}
		}
		else if constexpr (type == VariantType::OBJECT) {
			if constexpr (std::is_pointer_v<T>) {
				_data = static_cast<Reflected*>(value);
				set_class_info(value->get_class_name());
			}
			else {
				_data = static_cast<Reflected*>(&value);
				set_class_info(value.get_class_name());
			}
		}
		else if constexpr (type == VariantType::NIL) {
			_data = std::monostate {};
		}
		else if constexpr (type == VariantType::INVALID) {
			static_assert(false, "Variant type is unrecognized");
		}
		// default case if assignment is 1:1
		else {
			_data = std::move(value);
		}
	}

	// Copy and move constructors
	Variant(const Variant& other) = default;
	Variant(Variant&& other) noexcept = default;

	// Assignment operators
	Variant& operator=(const Variant& other) = default;
	Variant& operator=(Variant&& other) noexcept = default;

	// Type checking
	VariantType get_type() const { return _type; }
	bool is_type(VariantType type) const { return type == _type; }
	bool is_nil() const { return _type == VariantType::NIL; }

	// Type conversion with std::expected
	template <VariantCompatible T>
	std::expected<T, std::string> as() const {
		if (get_variant_type<T>() != _type)
			return std::unexpected("Variant type does not match requested type");

		if constexpr (std::is_same_v<T, bool>) {
			return std::get<bool>(_data);
		}
		else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
			return static_cast<T>(std::get<int>(_data));
		}
		else if constexpr (std::is_floating_point_v<T>) {
			return static_cast<T>(std::get<real_t>(_data));
		}
		else if constexpr (std::is_pointer_v<T> && is_reflected_class_type<std::remove_pointer_t<T>>) {
			return static_cast<T>(std::get<Reflected*>(_data));
		}
		else if constexpr (!std::is_pointer_v<T> && is_reflected_class_type<std::remove_reference_t<T>>) {
			return *static_cast<T*>(std::get<Reflected*>(_data));
		}
		else
			return std::get<T>(_data);
	}

	// Equality operators
	bool operator==(const Variant& other) const;

	bool operator!=(const Variant& other) const { return !(*this == other); }

	// String conversion for debugging
	std::string to_string() const;

	// Direct access to internal variant (for advanced usage)
	const InternalVariant& internal() const { return _data; }

	// Object property access
	std::string get_name() const;

	// Script-facing property/method access. These enforce reflection accessibility:
	// only members bound as AccessLevel::Public are reachable; protected/private
	// members return NIL (get/call) or are ignored (set).
	Variant get(std::string_view key) const;
	void set(std::string_view key, const Variant& value);
	// Object method call
	Variant call(std::string_view method_name);
	template <class... TArgs>
	Variant call(std::string_view method_name, TArgs&&... args) const;

	// Privileged property/method access for trusted engine systems (e.g. the
	// editor inspector). These bypass reflection accessibility checks and can
	// reach protected/private members. Do not expose to scripts.
	Variant get_internal(std::string_view key) const;
	void set_internal(std::string_view key, const Variant& value);
	Variant call_internal(std::string_view method_name);
	template <class... TArgs>
	Variant call_internal(std::string_view method_name, TArgs&&... args) const;

	// implicit conversion
	// In variant.h, inside the Variant class:

	// Implicit conversion operators
	explicit operator bool() const { return as<bool>().value(); }
	operator int() const { return as<int>().value(); }
	operator real_t() const { return as<real_t>().value(); }
	operator Vector2() const { return as<Vector2>().value(); }
	operator Vector3() const { return as<Vector3>().value(); }
	operator Color() const { return as<Color>().value(); }
	operator Vertex() const { return as<Vertex>().value(); }
	operator std::string() const { return as<std::string>().value(); }
	operator Path() const { return as<Path>().value(); }
	operator RID() const { return as<RID>().value(); }
	operator VariantArray() const { return as<VariantArray>().value(); }
	operator Reflected*() const { return as<Reflected*>().value(); }
};

template <class... TArgs>
Variant Variant::call(std::string_view method_name, TArgs&&... args) const {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	Variant params[] = { as<Reflected*>().value(), args... };
	return _internal_call(method_name, std::span<Variant>(params, sizeof...(args) + 1), true);
}

template <class... TArgs>
Variant Variant::call_internal(std::string_view method_name, TArgs&&... args) const {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	Variant params[] = { as<Reflected*>().value(), args... };
	return _internal_call(method_name, std::span<Variant>(params, sizeof...(args) + 1), false);
}

} // namespace feather