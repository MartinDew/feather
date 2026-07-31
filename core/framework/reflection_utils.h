#pragma once

#include <type_traits>

namespace feather {

// Forward-declared, not included: reflected.h includes this header before
// Reflected is defined, so including it back here would be circular.
// is_base_of_v below is a dependent expression, only resolved once a concrete
// T is checked -- by then reflected.h is fully included wherever it matters.
class Reflected;

template <class T>
concept is_reflected_class_type = requires { T::get_class_static(); };

// Value type (FSTRUCT / FCLASS(novtable)): reflected but not derived from
// Reflected, so no vtable pointer -- see ClassDB::register_value_class.
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