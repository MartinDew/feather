#pragma once

#include <type_traits>

namespace feather {

// Forward-declared, not included: reflected.h itself includes this header
// (before Reflected is defined) to get is_reflected_class_type/object_cast, so
// #include <framework/reflected.h> here would see an incomplete Reflected at
// best and a circular-include no-op at worst. is_base_of_v below only needs
// Reflected complete at the point a concrete T is actually checked against the
// concept (a dependent expression, resolved at instantiation) -- by then every
// translation unit that cares has reflected.h fully included.
class Reflected;

template <class T>
concept is_reflected_class_type = requires { T::get_class_static(); };

// A value type (FSTRUCT / FCLASS(novtable)): reflected, but deliberately NOT
// derived from Reflected — no vtable pointer, so it stays exactly as small as
// its plain members (the point of reflecting e.g. an ECS component). It has no
// object_cast/is_of_type polymorphism and no ClassDB factory; see
// ClassDB::register_value_class.
template <class T>
concept is_reflected_value_type = is_reflected_class_type<T> && !std::is_base_of_v<Reflected, T>;

template <class T, is_reflected_class_type T2>
	requires is_reflected_class_type<std::remove_const_t<T>>
T* object_cast(T2* object) {
	if constexpr (std::is_const_v<T2>)
		static_assert(std::is_const_v<T>, "Return type must be const for const object casts");

	if (object == nullptr) {
		return nullptr;
	}
	if (object->is_of_type(std::remove_const_t<T>::get_class_static())) {
		return static_cast<T*>(object);
	}
	return nullptr;
}

template <class T, is_reflected_class_type T2>
T* object_cast(T2& object) {
	return object_cast<T>(&object);
}

} // namespace feather