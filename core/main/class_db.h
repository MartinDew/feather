#pragma once

#include <framework/callable.h>
#include <framework/class_info.h>
#include <framework/reflection_utils.h>
#include <framework/singleton_helpers.h>
#include <framework/variant.h>
#include <framework/static_string.hpp>

#include <framework/delegate.h>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace feather {

class ClassDB {
	friend Variant;
	friend struct Main;
	FDECLARE_SINGLETON(ClassDB);

	ClassDB();

	std::map<StaticString, ClassInfo> _class_infos;
	std::map<StaticString, Delegate<const std::string_view>> _subclass_delegates;

	ClassInfo* _current_info = nullptr;

	static void _fire_subclass_delegates(std::string_view class_name);

	template <typename T, typename U>
	static constexpr size_t offset_of(U T::* member) {
		return reinterpret_cast<size_t>(&(static_cast<T*>(nullptr)->*member));
	}

	static std::vector<StaticString> _get_children_names_internal(const ClassInfo& object, bool exclusive = false);

	static ClassInfo* _get_class_info_internal(std::string_view name);

public:
	void print_db();

	static Callable get_static_method(const StaticString& class_name, std::string_view func_name);

	template <is_reflected_class_type T>
	static void register_class();

	template <is_reflected_class_type T>
	static void register_abstract_class();

	template <is_reflected_class_type T>
	static void register_singleton_class();

	// Register a value type (FSTRUCT / FCLASS(novtable)): properties (and
	// static methods) only, no Reflected base, no object_create_func -- there
	// is no vtable to construct a polymorphic factory through. See
	// is_reflected_value_type.
	template <is_reflected_value_type T>
	requires (!std::is_base_of_v<Reflected, T>)
	static void register_value_class();

	// Create a property backed directly by a data member (generates trivial
	// get/set accessors that read/write the member). Both accessors share the
	// member's accessibility.
	template <class T, class U>
	static void bind_property(U T::* member, std::string_view name, AccessLevel access = AccessLevel::Public);

	// Create a property backed by explicit getter/setter member functions. Used by
	// generated code for members that expose custom or generated accessors. Three
	// forms cover read/write, read-only and write-only properties (a null member
	// pointer can't be deduced, so absent accessors get their own overload). The
	// getter/setter accessibilities are tracked independently.
	template <class T, class TGet, class TSet>
	static void bind_property_accessors(
			TGet (T::*getter)() const,
			void (T::*setter)(TSet),
			std::string_view name,
			AccessLevel getter_access = AccessLevel::Public,
			AccessLevel setter_access = AccessLevel::Public
	);

	template <class T, class TGet>
	static void bind_property_get(TGet (T::*getter)() const, std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TSet>
	static void bind_property_set(void (T::*setter)(TSet), std::string_view name, AccessLevel access = AccessLevel::Public);

	// Guarded property binds: compile to a no-op when the property type isn't
	// Variant-marshalable. The generator emits these so a member of an
	// unmarshalable type (e.g. std::shared_ptr<...>) can still get generated
	// accessors without breaking the build — it simply isn't reflected.
	template <class T, class TGet, class TSet>
	static void bind_property_accessors_if_bindable(
			TGet (T::*getter)() const,
			void (T::*setter)(TSet),
			std::string_view name,
			AccessLevel getter_access = AccessLevel::Public,
			AccessLevel setter_access = AccessLevel::Public
	);

	template <class T, class TGet>
	static void bind_property_get_if_bindable(TGet (T::*getter)() const, std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TSet>
	static void bind_property_set_if_bindable(void (T::*setter)(TSet), std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TRet, class... TArgs>
	static void bind_method(TRet (T::*method)(TArgs...), std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TRet, class... TArgs>
	static void bind_method(
			TRet (T::*method)(TArgs...) const,
			std::string_view name,
			AccessLevel access = AccessLevel::Public
	);

	template <class TRet, class... TArgs>
	static void bind_static_method(TRet (*method)(TArgs...), std::string_view name, AccessLevel access = AccessLevel::Public);

	// Same as bind_method but compiles to a no-op when the signature isn't
	// Variant-marshalable. Emitted by the generator for auto-bound (opt-out)
	// methods so an unmarshalable public method is silently skipped instead of
	// breaking the build. Explicitly annotated methods use the strict bind_method.
	template <class T, class TRet, class... TArgs>
	static void bind_method_if_bindable(TRet (T::*method)(TArgs...), std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TRet, class... TArgs>
	static void bind_method_if_bindable(
			TRet (T::*method)(TArgs...) const,
			std::string_view name,
			AccessLevel access = AccessLevel::Public
	);

	// Returns an unmanaged raw pointer to a reflected object
	static Reflected* create_object_unsafe(std::string_view object_name);

	// A somewhat safer function that validates if the requested object can be cast in the specified T
	template <class T>
	static std::unique_ptr<T> create_object(std::string_view object_name) {
		Reflected* object = create_object_unsafe(object_name);
		std::unique_ptr<T> ptr { object_cast<T>(object) };
		return ptr;
	}

	static Delegate<std::string_view>::id_t on_subclass_registered(
			std::string_view base_class_name,
			const Delegate<std::string_view>::DelegateFuncType& callback
	);

	static std::vector<StaticString> get_children_names(std::string_view object_name, bool exclusive = false);

	static std::string get_children_names_string(StaticString object_name, bool exclusive = false);

	static bool has_parent(StaticString object_name, StaticString parent_name);
};

} //namespace feather

#include "class_db.inl"