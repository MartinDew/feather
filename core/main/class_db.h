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

public:
	using subclass_delegate_t = Delegate<const std::string_view>;

private:
	std::map<StaticString, ClassInfo> _class_infos;
	std::map<StaticString, subclass_delegate_t> _subclass_delegates;
	std::map<StaticString, EnumInfo> _enum_infos;

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

	// path == "-" writes to stdout. Describes the engine's own reflected API
	// only -- called before index_project(), so no project/plugin type is ever
	// included (see docs/plugin_abi.md).
	static void dump_api_json(const std::string& path);

	static Callable get_static_method(const StaticString& class_name, std::string_view func_name);

	// A free enum reflected alongside a class (e.g. LightType next to Light).
	// `values` names must already be stable storage (generated code only ever
	// passes string literals, same convention as register_extension_class).
	static void register_enum(std::string_view name, VariantType underlying,
							  std::vector<std::pair<std::string_view, int64_t>> values);
	static const EnumInfo* get_enum_info(std::string_view name);

	template <is_reflected_class_type T>
	static void register_class();

	template <is_reflected_class_type T>
	static void register_abstract_class();

	template <is_reflected_class_type T>
	static void register_singleton_class();

	// Non-template registration for a runtime-named class (the plugin ABI's
	// classdb_register_extension_class) -- there's no C++ type to bind members
	// through, so the caller supplies the finished factory directly. `name`
	// and `parent` must already be stable storage (e.g. interned): ClassInfo
	// keys on StaticString, a non-owning view.
	static void register_extension_class(std::string_view name, std::string_view parent, std::function<Variant()> factory);

	// Value type (FSTRUCT / FCLASS(novtable)): properties/static methods only,
	// no Reflected base or object_create_func -- no vtable to factory through.
	template <is_reflected_value_type T>
		requires(!std::is_base_of_v<Reflected, T>)
	static void register_value_class();

	// Property backed directly by a data member; both accessors share its access level.
	template <class T, class U>
	static void bind_property(U T::* member, std::string_view name, AccessLevel access = AccessLevel::Public);

	// Property backed by explicit getter/setter member functions, with
	// independent accessibility per accessor. Read-only/write-only overloads
	// exist because a null member pointer can't be deduced.
	template <class T, class TGet, class TSet>
	static void bind_property_accessors(TGet (T::*getter)() const,
										void (T::*setter)(TSet),
										std::string_view name,
										AccessLevel getter_access = AccessLevel::Public,
										AccessLevel setter_access = AccessLevel::Public);

	template <class T, class TGet>
	static void
	bind_property_get(TGet (T::*getter)() const, std::string_view name, AccessLevel access = AccessLevel::Public);

	template <class T, class TSet>
	static void
	bind_property_set(void (T::*setter)(TSet), std::string_view name, AccessLevel access = AccessLevel::Public);

	// Property backed by a data member with no Variant mapping at all (e.g. a
	// value type's raw physical layout) -- goes into ClassInfo::fields, not
	// ::properties. Never guarded/no-op like bind_property_*_if_bindable:
	// layout is layout regardless of whether the type is script-marshalable.
	template <class T, class U>
	static void bind_field(U T::* member, std::string_view name);

	// Same, but the member's declared type is a registered enum (register_enum)
	// rather than something get_variant_class_name<U>() can name on its own --
	// the generator passes the enum's name explicitly.
	template <class T, class U>
	static void bind_field(U T::* member, std::string_view name, std::string_view type_class_override);

	// Property whose C++ type is an enum class: stored as its underlying
	// integer (get_variant_type<enum>() is INVALID, so bind_property_accessors
	// would silently no-op via the _if_bindable guard) with type_class set to
	// the enum's name so a consumer can still recover it.
	template <class T, class TGet, class TSet>
	static void bind_enum_property_accessors(TGet (T::*getter)() const,
											 void (T::*setter)(TSet),
											 std::string_view name,
											 std::string_view enum_type_name,
											 AccessLevel getter_access = AccessLevel::Public,
											 AccessLevel setter_access = AccessLevel::Public);

	template <class T, class TGet>
	static void bind_enum_property_get(TGet (T::*getter)() const,
									   std::string_view name,
									   std::string_view enum_type_name,
									   AccessLevel access = AccessLevel::Public);

	template <class T, class TSet>
	static void bind_enum_property_set(void (T::*setter)(TSet),
									   std::string_view name,
									   std::string_view enum_type_name,
									   AccessLevel access = AccessLevel::Public);

	// Guarded binds: no-op when the property type isn't Variant-marshalable
	// (e.g. std::shared_ptr<...>), so generated accessors never break the build.
	template <class T, class TGet, class TSet>
	static void bind_property_accessors_if_bindable(TGet (T::*getter)() const,
													void (T::*setter)(TSet),
													std::string_view name,
													AccessLevel getter_access = AccessLevel::Public,
													AccessLevel setter_access = AccessLevel::Public);

	template <class T, class TGet>
	static void bind_property_get_if_bindable(TGet (T::*getter)() const,
											  std::string_view name,
											  AccessLevel access = AccessLevel::Public);

	template <class T, class TSet>
	static void bind_property_set_if_bindable(void (T::*setter)(TSet),
											  std::string_view name,
											  AccessLevel access = AccessLevel::Public);

	// param_names/is_virtual are best-effort metadata captured from the
	// declaration text (generate_reflection.py) for a typed plugin-side
	// mirror -- they don't affect dispatch, so a hand-written bind_method call
	// can safely omit them.
	template <class T, class TRet, class... TArgs>
	static void bind_method(TRet (T::*method)(TArgs...), std::string_view name,
							AccessLevel access = AccessLevel::Public,
							std::initializer_list<std::string_view> param_names = {},
							bool is_virtual = false);

	template <class T, class TRet, class... TArgs>
	static void bind_method(TRet (T::*method)(TArgs...) const, std::string_view name,
							AccessLevel access = AccessLevel::Public,
							std::initializer_list<std::string_view> param_names = {},
							bool is_virtual = false);

	template <class TRet, class... TArgs>
	static void bind_static_method(TRet (*method)(TArgs...), std::string_view name,
								   AccessLevel access = AccessLevel::Public,
								   std::initializer_list<std::string_view> param_names = {});

	// Guarded bind_method: no-op when the signature isn't Variant-marshalable.
	// Used for auto-bound (opt-out) methods; explicit [[method]] uses bind_method directly.
	template <class T, class TRet, class... TArgs>
	static void bind_method_if_bindable(TRet (T::*method)(TArgs...),
										std::string_view name,
										AccessLevel access = AccessLevel::Public,
										std::initializer_list<std::string_view> param_names = {},
										bool is_virtual = false);

	template <class T, class TRet, class... TArgs>
	static void bind_method_if_bindable(TRet (T::*method)(TArgs...) const,
										std::string_view name,
										AccessLevel access = AccessLevel::Public,
										std::initializer_list<std::string_view> param_names = {},
										bool is_virtual = false);

	// Raw ClassInfo* for a registered class, or nullptr. Used to hand the
	// plugin ABI an opaque FeatherClassPtr handle (core/extension) -- callers
	// never dereference the fields directly except through ClassDB itself.
	static ClassInfo* get_class_info(std::string_view name);

	// Returns an unmanaged raw pointer to a reflected object
	static Reflected* create_object_unsafe(std::string_view object_name);

	// A somewhat safer function that validates if the requested object can be cast in the specified T
	template <class T>
	static std::unique_ptr<T> create_object(std::string_view object_name) {
		Reflected* object = create_object_unsafe(object_name);
		std::unique_ptr<T> ptr { object_cast<T>(object) };
		return ptr;
	}

	static Delegate<std::string_view>::id_t
	on_subclass_registered(std::string_view base_class_name,
						   const Delegate<std::string_view>::DelegateFuncType& callback);

	static void unregister_subclass_delegate(std::string_view base_class_name, Delegate<std::string_view>::id_t id);

	static void unregister_class(std::string_view name);

	static std::vector<StaticString> get_children_names(std::string_view object_name, bool exclusive = false);

	static std::string get_children_names_string(StaticString object_name, bool exclusive = false);

	static bool has_parent(StaticString object_name, StaticString parent_name);
};

} //namespace feather

#include "class_db.inl"