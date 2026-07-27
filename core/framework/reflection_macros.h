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

// Base declarator will give add the necessary logic for reflection to work with this class
#define FCLASS(_name, _parent)                                                                                         \
	friend class ClassDB;                                                                                              \
	struct _class_type {};                                                                                             \
	template <class T>                                                                                                 \
	friend void has_bind_method(const T& t);                                                                           \
                                                                                                                       \
protected:                                                                                                             \
	using Type = _name;                                                                                                \
	using Super = _parent;                                                                                             \
                                                                                                                       \
public:                                                                                                                \
	constexpr static StaticString get_class_static() {                                                                 \
		return #_name##_ss;                                                                                            \
	}                                                                                                                  \
	constexpr static StaticString get_parent_name() {                                                                  \
		return #_parent##_ss;                                                                                          \
	}                                                                                                                  \
	/*Maybe there's a better way to do casts than use vcalls? */                                                       \
	bool is_of_type(StaticString type_name) const override {                                                           \
		return get_class_static() == type_name || Super::is_of_type(type_name);                                        \
	}                                                                                                                  \
	virtual StaticString get_class_name() override {                                                                   \
		return get_class_static();                                                                                     \
	}                                                                                                                  \
                                                                                                                       \
private:

// Reflection declarator that will help the generator understand this class is abstract
#define FCLASS_ABSTRACT FCLASS

// Reflection declarator that will help the generator consider this class has a singleton
#define FCLASS_SINGLETON(_name, _parent)                                                                               \
	FCLASS(_name, _parent);                                                                                            \
	FDECLARE_SINGLETON(_name);
