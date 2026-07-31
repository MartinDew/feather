#include "variant.h"

#include "assert.h"
#include "framework/reflected.h"
#include <main/class_db.h>

namespace feather {

Variant::Variant(Reflected& ref) : Variant(&ref) {
}

bool Variant::operator==(const Variant& other) const {
	// Types must match
	if (_type != other._type) {
		return false;
	}

	// Use std::visit to compare the values
	return std::visit(
			[](const auto& lhs, const auto& rhs) -> bool {
				using LhsType = std::decay_t<decltype(lhs)>;
				using RhsType = std::decay_t<decltype(rhs)>;

				// If types don't match at compile time, return false
				if constexpr (!std::is_same_v<LhsType, RhsType>) {
					return false;
				}
				// monostate (NIL) is always equal
				else if constexpr (std::is_same_v<LhsType, std::monostate>) {
					return true;
				}
				// For all other types, use their operator==
				else {
					return lhs == rhs;
				}
			},
			_data,
			other._data);
}

std::string Variant::to_string() const {
	switch (_type) {
	case VariantType::NIL:
		return "nil";
	case VariantType::BOOL:
		return std::get<bool>(_data) ? "true" : "false";
	case VariantType::INT:
		return std::to_string(std::get<int>(_data));
	case VariantType::FLOAT:
		return std::to_string(std::get<real_t>(_data));
	case VariantType::STRING:
		return std::get<std::string>(_data);
	case VariantType::VECTOR2: {
		auto& v = std::get<Vector2>(_data);
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
	}
	case VariantType::VECTOR3: {
		auto& v = std::get<Vector3>(_data);
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}
	case VariantType::COLOR: {
		auto& c = std::get<Color>(_data);
		return "rgba(" + std::to_string(c.r()) + ", " + std::to_string(c.g()) + ", " + std::to_string(c.b()) + ", " +
				std::to_string(c.a()) + ")";
	}
	case VariantType::VERTEX:
		return "[Vertex]"; // expand if Vertex gains a meaningful string form
	case VariantType::PATH:
		return std::get<Path>(_data).string(); // assumes Path has to_string()
	case VariantType::RID:
		return "RID(" + std::to_string(std::get<RID>(_data).id) + ")"; // assumes RID has .id
	case VariantType::ARRAY:
		return "[Array]";
	case VariantType::OBJECT:
		return "[Object] : " + get_name();
	case VariantType::INVALID:
		return "[Invalid]";
	}
	return "[Unknown]";
}

std::string Variant::get_name() const {
	if (_type != VariantType::OBJECT) {
		return to_string();
	}
	if (!_object_info) {
		return "[Object: no class info]";
	}
	return _object_info->name;
}

Variant Variant::get(std::string_view key) const {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	if (!_object_info) {
		return {};
	}
	auto info = _object_info;
	while (info) {
		for (auto& property : info->properties) {
			if (property.name == key) {
				// Script-facing access: only public getters are reachable.
				if (property.getter_access != AccessLevel::Public || !property.getter) {
					return {};
				}
				return property.getter(std::get<Reflected*>(_data));
			}
		}
		info = ClassDB::_get_class_info_internal(info->parent);
	}
	return {}; // property not found
}

Variant Variant::get_internal(std::string_view key) const {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	if (!_object_info) {
		return {};
	}
	auto info = _object_info;
	while (info) {
		for (auto& property : info->properties) {
			if (property.name == key) {
				if (!property.getter) {
					return {};
				}
				return property.getter(std::get<Reflected*>(_data));
			}
		}
		info = ClassDB::_get_class_info_internal(info->parent);
	}
	return {}; // property not found
}

Variant Variant::call(std::string_view method_name) {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	if (!_object_info) {
		return {};
	}
	Variant self = as<Reflected*>().value();
	return _internal_call(method_name, std::span<Variant>(&self, 1), /*enforce_public=*/true);
}

Variant Variant::call_internal(std::string_view method_name) {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	if (!_object_info) {
		return {};
	}
	Variant self = as<Reflected*>().value();
	return _internal_call(method_name, std::span<Variant>(&self, 1), /*enforce_public=*/false);
}

void Variant::set(std::string_view key, const Variant& value) {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	auto info = _object_info;
	while (info) {
		for (auto& property : info->properties) {
			if (property.name == key) {
				// Script-facing access: only public setters are writable.
				if (property.setter_access != AccessLevel::Public || !property.setter) {
					return;
				}
				property.setter(std::get<Reflected*>(_data), value);
				return;
			}
		}
		info = ClassDB::_get_class_info_internal(info->parent);
	}
}

void Variant::set_internal(std::string_view key, const Variant& value) {
	fassert(_type == VariantType::OBJECT, "Variant is not an object");
	auto info = _object_info;
	while (info) {
		for (auto& property : info->properties) {
			if (property.name == key) {
				if (!property.setter) {
					return;
				}
				property.setter(std::get<Reflected*>(_data), value);
				return;
			}
		}
		info = ClassDB::_get_class_info_internal(info->parent);
	}
}

void Variant::set_class_info(StaticString class_name) {
	_object_info = ClassDB::get()->_get_class_info_internal(class_name);
}

Variant Variant::_internal_call(std::string_view method_name, std::span<Variant> args, bool enforce_public) const {
	auto info = _object_info;
	while (info) {
		for (auto& method : info->methods) {
			if (method.name == method_name) {
				// Script-facing calls (enforce_public) only reach public methods.
				if (enforce_public && method.access != AccessLevel::Public) {
					return {};
				}
				Callable& callable = method.callable;

				return callable.call(args);
			}
		}
		info = ClassDB::_get_class_info_internal(info->parent);
	}

	return {};
}

} //namespace feather