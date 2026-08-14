#include "feather_interface.h"

#include "extension_interface.h"
#include <framework/reflected.h>
#include <main/class_db.h>
#include <math/math_defs.h>
#include <resources/rid.h>

#include <array>
#include <cstring>
#include <iostream>
#include <string_view>

using namespace feather;

// Keeps FeatherVariantType numerically identical to feather::VariantType --
// the marshaling switches below index by this value.
static_assert(static_cast<int>(FEATHER_VARIANT_NIL) == static_cast<int>(VariantType::NIL));
static_assert(static_cast<int>(FEATHER_VARIANT_BOOL) == static_cast<int>(VariantType::BOOL));
static_assert(static_cast<int>(FEATHER_VARIANT_INT) == static_cast<int>(VariantType::INT));
static_assert(static_cast<int>(FEATHER_VARIANT_FLOAT) == static_cast<int>(VariantType::FLOAT));
static_assert(static_cast<int>(FEATHER_VARIANT_VECTOR3) == static_cast<int>(VariantType::VECTOR3));
static_assert(static_cast<int>(FEATHER_VARIANT_VECTOR2) == static_cast<int>(VariantType::VECTOR2));
static_assert(static_cast<int>(FEATHER_VARIANT_VERTEX) == static_cast<int>(VariantType::VERTEX));
static_assert(static_cast<int>(FEATHER_VARIANT_COLOR) == static_cast<int>(VariantType::COLOR));
static_assert(static_cast<int>(FEATHER_VARIANT_RID) == static_cast<int>(VariantType::RID));
static_assert(static_cast<int>(FEATHER_VARIANT_STRING) == static_cast<int>(VariantType::STRING));
static_assert(static_cast<int>(FEATHER_VARIANT_ARRAY) == static_cast<int>(VariantType::ARRAY));
static_assert(static_cast<int>(FEATHER_VARIANT_PATH) == static_cast<int>(VariantType::PATH));
static_assert(static_cast<int>(FEATHER_VARIANT_OBJECT) == static_cast<int>(VariantType::OBJECT));
static_assert(static_cast<int>(FEATHER_VARIANT_INVALID) == static_cast<int>(VariantType::INVALID));

// FeatherVector2/3/Color/Vertex are plain `float`; a plugin has no access to
// `real_t`, so a double-precision engine build would silently corrupt these.
static_assert(sizeof(real_t) == sizeof(float), "plugin ABI ptrcall layouts assume real_t == float");

namespace {

// The types method_ptrcall/variant_from_ptr/variant_to_ptr can marshal
// directly. STRING/PATH/ARRAY have no fixed C layout -- see the header.
Variant variant_from_c_layout(VariantType type, const void* src) {
	switch (type) {
	case VariantType::NIL:
		return Variant();
	case VariantType::BOOL:
		return Variant(*static_cast<const FeatherBool*>(src) != 0);
	case VariantType::INT:
		return Variant(*static_cast<const int32_t*>(src));
	case VariantType::FLOAT:
		return Variant(static_cast<real_t>(*static_cast<const float*>(src)));
	case VariantType::RID: {
		RID rid { *static_cast<const uint64_t*>(src) };
		return Variant(rid);
	}
	case VariantType::OBJECT: {
		// Reflected* is VariantCompatible, but Variant's own pointer ctor
		// dereferences it unconditionally (get_class_name()) -- guard here
		// rather than crash on a legitimately-null object argument.
		Reflected* obj = *static_cast<Reflected* const*>(src);
		return obj ? Variant(obj) : Variant();
	}
	case VariantType::VECTOR2: {
		auto* v = static_cast<const FeatherVector2*>(src);
		return Variant(Vector2(v->x, v->y));
	}
	case VariantType::VECTOR3: {
		auto* v = static_cast<const FeatherVector3*>(src);
		return Variant(Vector3(v->x, v->y, v->z));
	}
	case VariantType::COLOR: {
		auto* c = static_cast<const FeatherColor*>(src);
		return Variant(Color(c->r, c->g, c->b, c->a));
	}
	case VariantType::VERTEX: {
		auto* v = static_cast<const FeatherVertex*>(src);
		return Variant(Vertex(Vector3(v->position.x, v->position.y, v->position.z),
				Vector3(v->normal.x, v->normal.y, v->normal.z), Vector2(v->uv.x, v->uv.y)));
	}
	default:
		fassert(false, "variant_from_c_layout: type has no fixed C layout (STRING/PATH/ARRAY?)");
		return Variant();
	}
}

void variant_to_c_layout(VariantType type, const Variant& value, void* dst) {
	if (!dst)
		return;
	switch (type) {
	case VariantType::NIL:
		return;
	case VariantType::BOOL:
		*static_cast<FeatherBool*>(dst) = value.as<bool>().value() ? 1 : 0;
		return;
	case VariantType::INT:
		*static_cast<int32_t*>(dst) = value.as<int>().value();
		return;
	case VariantType::FLOAT:
		*static_cast<float*>(dst) = static_cast<float>(value.as<real_t>().value());
		return;
	case VariantType::RID:
		*static_cast<uint64_t*>(dst) = value.as<RID>().value().id;
		return;
	case VariantType::OBJECT:
		*static_cast<Reflected**>(dst) = value.is_nil() ? nullptr : value.as<Reflected*>().value();
		return;
	case VariantType::VECTOR2: {
		auto v = value.as<Vector2>().value();
		*static_cast<FeatherVector2*>(dst) = { v.x, v.y };
		return;
	}
	case VariantType::VECTOR3: {
		auto v = value.as<Vector3>().value();
		*static_cast<FeatherVector3*>(dst) = { v.x, v.y, v.z };
		return;
	}
	case VariantType::COLOR: {
		auto c = value.as<Color>().value();
		*static_cast<FeatherColor*>(dst) = { c.x, c.y, c.z, c.w };
		return;
	}
	case VariantType::VERTEX: {
		auto v = value.as<Vertex>().value();
		*static_cast<FeatherVertex*>(dst) = { { v.position.x, v.position.y, v.position.z },
			{ v.normal.x, v.normal.y, v.normal.z }, { v.uv.x, v.uv.y } };
		return;
	}
	default:
		fassert(false, "variant_to_c_layout: type has no fixed C layout (STRING/PATH/ARRAY?)");
	}
}

// Builds the Variant argument span a Callable expects: the receiver first
// (for an instance method), then the declared params, converted from their
// fixed C layout. `args` is the ptrcall convention -- one raw pointer per
// declared parameter.
template <class ArgAt>
Variant invoke_callable(ClassInfo::Method* method, FeatherObjectPtr obj, ArgAt&& arg_variant_at) {
	size_t n = method->param_types.size();
	fassert(n + 1 <= 16, "method has more parameters than the plugin ABI's fixed call buffer supports");
	std::array<Variant, 16> stack_args;
	size_t idx = 0;
	if (!method->is_static) {
		Reflected* self = static_cast<Reflected*>(obj);
		stack_args[idx++] = self ? Variant(self) : Variant();
	}
	for (size_t i = 0; i < n; ++i) {
		stack_args[idx++] = arg_variant_at(i);
	}
	return method->callable.call(std::span<Variant>(stack_args.data(), idx));
}

// ---- extern "C" interface functions, resolved by name via get_proc_address ----

extern "C" {

void feather_log(const char* message) {
	std::cout << "[extension] " << message << std::endl;
}

FeatherClassPtr classdb_get_class(const char* class_name) {
	return ClassDB::get_class_info(class_name);
}

FeatherMethodPtr classdb_class_get_method(FeatherClassPtr cls, const char* method_name) {
	auto* info = static_cast<ClassInfo*>(cls);
	if (!info)
		return nullptr;
	for (auto& method : info->methods) {
		if (method.name == method_name)
			return &method;
	}
	return nullptr;
}

FeatherObjectPtr object_create(const char* class_name) {
	return ClassDB::create_object_unsafe(class_name);
}

void object_destroy(FeatherObjectPtr object) {
	delete static_cast<Reflected*>(object);
}

void method_ptrcall(FeatherMethodPtr method_ptr, FeatherObjectPtr obj, const void* const* args, void* ret) {
	auto* method = static_cast<ClassInfo::Method*>(method_ptr);
	Variant result =
			invoke_callable(method, obj, [&](size_t i) { return variant_from_c_layout(method->param_types[i], args[i]); });
	if (method->return_type != VariantType::NIL) {
		variant_to_c_layout(method->return_type, result, ret);
	}
}

void method_variant_call(
		FeatherMethodPtr method_ptr, FeatherObjectPtr obj, const FeatherVariantPtr* args, size_t argc, FeatherVariantPtr r_ret) {
	auto* method = static_cast<ClassInfo::Method*>(method_ptr);
	fassert(argc == method->param_types.size(), "method_variant_call: argument count doesn't match the bound method");
	Variant result = invoke_callable(method, obj, [&](size_t i) { return *static_cast<Variant*>(args[i]); });
	if (r_ret) {
		*static_cast<Variant*>(r_ret) = std::move(result);
	}
}

FeatherBool object_get_property(FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr r_out) {
	auto* info = static_cast<ClassInfo*>(cls);
	while (info) {
		for (auto& prop : info->properties) {
			if (prop.name == prop_name) {
				if (!prop.getter)
					return 0;
				if (r_out)
					*static_cast<Variant*>(r_out) = prop.getter(obj);
				return 1;
			}
		}
		info = ClassDB::get_class_info(info->parent.str());
	}
	return 0;
}

FeatherBool object_set_property(FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr value) {
	auto* info = static_cast<ClassInfo*>(cls);
	while (info) {
		for (auto& prop : info->properties) {
			if (prop.name == prop_name) {
				if (!prop.setter)
					return 0;
				prop.setter(obj, *static_cast<Variant*>(value));
				return 1;
			}
		}
		info = ClassDB::get_class_info(info->parent.str());
	}
	return 0;
}

FeatherVariantPtr variant_new() {
	return new Variant();
}

void variant_destroy(FeatherVariantPtr variant) {
	delete static_cast<Variant*>(variant);
}

FeatherVariantType variant_get_type(FeatherVariantPtr variant) {
	return static_cast<FeatherVariantType>(static_cast<Variant*>(variant)->get_type());
}

void variant_from_ptr(FeatherVariantPtr dst, FeatherVariantType type, const void* src) {
	*static_cast<Variant*>(dst) = variant_from_c_layout(static_cast<VariantType>(type), src);
}

FeatherBool variant_to_ptr(FeatherVariantPtr src, FeatherVariantType type, void* dst) {
	auto* v = static_cast<Variant*>(src);
	if (v->get_type() != static_cast<VariantType>(type))
		return 0;
	variant_to_c_layout(static_cast<VariantType>(type), *v, dst);
	return 1;
}

size_t variant_get_string_utf8(FeatherVariantPtr variant, char* dst, size_t cap) {
	auto* v = static_cast<Variant*>(variant);
	std::string value = v->get_type() == VariantType::PATH ? v->as<Path>().value().string() : v->as<std::string>().value();
	if (dst && cap > 0) {
		size_t n = std::min(cap - 1, value.size());
		std::memcpy(dst, value.data(), n);
		dst[n] = '\0';
	}
	return value.size();
}

void variant_set_string_utf8(FeatherVariantPtr variant, const char* src, size_t len) {
	*static_cast<Variant*>(variant) = Variant(std::string(src, len));
}

} // extern "C"

// name -> function pointer, linear scan; called rarely (at plugin init only).
struct ProcEntry {
	std::string_view name;
	FeatherProc fn;
};

// clang-format off
const std::array<ProcEntry, 16> PROC_TABLE {{
	{ "feather_log",              reinterpret_cast<FeatherProc>(&feather_log) },
	{ "classdb_get_class",        reinterpret_cast<FeatherProc>(&classdb_get_class) },
	{ "classdb_class_get_method", reinterpret_cast<FeatherProc>(&classdb_class_get_method) },
	{ "object_create",            reinterpret_cast<FeatherProc>(&object_create) },
	{ "object_destroy",           reinterpret_cast<FeatherProc>(&object_destroy) },
	{ "method_ptrcall",           reinterpret_cast<FeatherProc>(&method_ptrcall) },
	{ "method_variant_call",      reinterpret_cast<FeatherProc>(&method_variant_call) },
	{ "object_get_property",      reinterpret_cast<FeatherProc>(&object_get_property) },
	{ "object_set_property",      reinterpret_cast<FeatherProc>(&object_set_property) },
	{ "variant_new",              reinterpret_cast<FeatherProc>(&variant_new) },
	{ "variant_destroy",          reinterpret_cast<FeatherProc>(&variant_destroy) },
	{ "variant_get_type",         reinterpret_cast<FeatherProc>(&variant_get_type) },
	{ "variant_from_ptr",         reinterpret_cast<FeatherProc>(&variant_from_ptr) },
	{ "variant_to_ptr",           reinterpret_cast<FeatherProc>(&variant_to_ptr) },
	{ "variant_get_string_utf8",  reinterpret_cast<FeatherProc>(&variant_get_string_utf8) },
	{ "variant_set_string_utf8",  reinterpret_cast<FeatherProc>(&variant_set_string_utf8) },
}};
// clang-format on

} // namespace

namespace feather {

FeatherProc feather_get_proc_address(const char* name) {
	std::string_view needle(name);
	for (auto& entry : PROC_TABLE) {
		if (entry.name == needle)
			return entry.fn;
	}
	return nullptr;
}

} // namespace feather
