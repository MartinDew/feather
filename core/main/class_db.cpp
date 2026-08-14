#include "class_db.h"
#include "framework/reflected.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <sstream>

#include <print>

namespace feather {

namespace {

constexpr const char* variant_type_name(VariantType type) {
	switch (type) {
	case VariantType::NIL:
		return "NIL";
	case VariantType::BOOL:
		return "BOOL";
	case VariantType::INT:
		return "INT";
	case VariantType::FLOAT:
		return "FLOAT";
	case VariantType::VECTOR3:
		return "VECTOR3";
	case VariantType::VECTOR2:
		return "VECTOR2";
	case VariantType::VERTEX:
		return "VERTEX";
	case VariantType::COLOR:
		return "COLOR";
	case VariantType::RID:
		return "RID";
	case VariantType::STRING:
		return "STRING";
	case VariantType::ARRAY:
		return "ARRAY";
	case VariantType::PATH:
		return "PATH";
	case VariantType::OBJECT:
		return "OBJECT";
	case VariantType::INVALID:
		return "INVALID";
	}
	return "INVALID";
}

constexpr const char* access_level_name(AccessLevel access) {
	switch (access) {
	case AccessLevel::Public:
		return "public";
	case AccessLevel::Protected:
		return "protected";
	case AccessLevel::Private:
		return "private";
	}
	return "public";
}

// Every name that flows through here is a C++ identifier -- this only guards
// against the pathological case, it isn't a general JSON string escaper.
std::string json_escape(std::string_view str) {
	std::string out;
	out.reserve(str.size());
	for (char c : str) {
		if (c == '"' || c == '\\')
			out += '\\';
		out += c;
	}
	return out;
}

void write_json_string(std::ostream& out, std::string_view str) {
	out << '"' << json_escape(str) << '"';
}

} // namespace

FSINGLETON_INSTANCE(ClassDB);

ClassDB::ClassDB() {
	FSINGLETON_CONSTRUCT_INSTANCE()
	_class_infos.insert(std::make_pair("Reflected", ClassInfo { .name = "Reflected"_ss, .parent = ""_ss }));
}

Reflected* ClassDB::create_object_unsafe(std::string_view name) {
	auto object_info_it = _instance->_class_infos.find(name);
	if (object_info_it != _instance->_class_infos.end()) {
		return object_info_it->second.object_create_func().as<Reflected*>().value();
	}

	return {};
}

std::vector<StaticString> ClassDB::_get_children_names_internal(const ClassInfo& object, bool exclusive) {
	std::vector<StaticString> children;
	children.reserve(object.children.size());
	for (auto& child : object.children) {
		children.push_back(child->name);
	}

	for (auto& child : object.children) {
		auto sub_children = _get_children_names_internal(*child, exclusive);
		children.append_range(sub_children);
	}
	return children;
}

ClassInfo* ClassDB::_get_class_info_internal(std::string_view name) {
	if (auto it = get()->_class_infos.find(name); it != get()->_class_infos.end()) {
		return &it->second;
	};
	return nullptr;
}

std::vector<StaticString> ClassDB::get_children_names(std::string_view object_name, bool exclusive) {
	if (auto it = ClassDB::get()->_class_infos.find(object_name); it != ClassDB::get()->_class_infos.end()) {
		return _get_children_names_internal(it->second, exclusive);
	}

	return {};
}

std::string ClassDB::get_children_names_string(StaticString object_name, bool exclusive) {
	std::string children_str;
	auto children = get_children_names(object_name, exclusive);
	for (auto& child : children) {
		children_str += child.str();
		children_str += " ";
	}
	return children_str;
}

Delegate<std::string_view>::id_t
ClassDB::on_subclass_registered(std::string_view base_class_name,
								const Delegate<std::string_view>::DelegateFuncType& callback) {
	return get()->_subclass_delegates[StaticString(base_class_name)].subscribe(callback);
}

void ClassDB::unregister_subclass_delegate(std::string_view base_class_name, Delegate<std::string_view>::id_t id) {
	auto it = get()->_subclass_delegates.find(StaticString(base_class_name));
	if (it != get()->_subclass_delegates.end()) {
		it->second.remove(id);
	}
}

// Callers must unregister a class's own children first -- this only detaches
// `name` from its parent and erases it, it doesn't recurse into `children`.
void ClassDB::unregister_class(std::string_view name) {
	ClassInfo* ci = _get_class_info_internal(name);
	if (!ci) {
		return;
	}

	if (ci->parent != ""_ss) {
		if (auto parent_it = get()->_class_infos.find(ci->parent); parent_it != get()->_class_infos.end()) {
			auto& siblings = parent_it->second.children;
			std::erase(siblings, ci);
		}
	}

	get()->_subclass_delegates.erase(StaticString(name));
	get()->_class_infos.erase(StaticString(name));
}

void ClassDB::_fire_subclass_delegates(std::string_view class_name) {
	ClassInfo* ci = _get_class_info_internal(class_name);
	while (ci && ci->parent != ""_ss) {
		auto it = get()->_subclass_delegates.find(ci->parent);
		if (it != get()->_subclass_delegates.end()) {
			it->second.execute(class_name);
		}
		ci = _get_class_info_internal(ci->parent);
	}
}

bool ClassDB::has_parent(StaticString object_name, StaticString parent_name) {
	ClassInfo* ci = _get_class_info_internal(object_name);
	while (ci && ci->parent != ""_ss) {
		if (ci->parent == parent_name) {
			return true;
		}
		ci = _get_class_info_internal(ci->parent);
	}
	return false;
}

void ClassDB::print_db() {
#ifdef BETA
	std::println("Printing database");

	for (auto& [name, info] : _class_infos) {
		std::println("Class: {} : ", info.name.str());
		for (auto& prop : info.properties) {
			std::println("\tProperty: {} Type: {}\n", prop.name.str(), std::to_underlying<VariantType>(prop.type));
		}

		for (auto& method : info.methods) {
			std::print("\tMethod: {} Return: {} Args: (", method.name.str(),
					std::to_underlying<VariantType>(method.return_type));
			for (size_t i = 0; i < method.param_types.size(); ++i) {
				if (i)
					std::print(", ");
				std::print("{}", std::to_underlying<VariantType>(method.param_types[i]));
			}
			std::println(")");
		}

		std::println("Children : {}", get_children_names_string(name, false));
	}
#endif
}

void ClassDB::dump_api_json(const std::string& path) {
	std::vector<const ClassInfo*> classes;
	classes.reserve(get()->_class_infos.size());
	for (auto& [name, info] : get()->_class_infos) {
		classes.push_back(&info);
	}
	// Map order is by StaticString's hash (its only comparable value, see
	// static_string.hpp), not name -- sort here so the dump is stable across
	// runs and diffable, which the generator relies on (write_if_changed).
	std::ranges::sort(classes, {}, [](const ClassInfo* ci) { return ci->name.str(); });

	std::ostringstream buf;
	buf << "{\n  \"classes\": [\n";
	for (size_t ci = 0; ci < classes.size(); ++ci) {
		const ClassInfo& info = *classes[ci];
		buf << "    {\n      \"name\": ";
		write_json_string(buf, info.name.str());
		buf << ",\n      \"parent\": ";
		write_json_string(buf, info.parent.str());
		buf << ",\n      \"is_abstract\": " << (info.is_abstract ? "true" : "false");
		buf << ",\n      \"is_singleton\": " << (info.is_singleton ? "true" : "false");
		buf << ",\n      \"is_value_type\": " << (info.is_value_type ? "true" : "false");

		buf << ",\n      \"properties\": [";
		for (size_t i = 0; i < info.properties.size(); ++i) {
			const auto& prop = info.properties[i];
			buf << (i ? ",\n        {\n" : "\n        {\n");
			buf << "          \"name\": ";
			write_json_string(buf, prop.name.str());
			buf << ",\n          \"type\": \"" << variant_type_name(prop.type) << "\"";
			buf << ",\n          \"has_getter\": " << (prop.getter ? "true" : "false");
			buf << ",\n          \"has_setter\": " << (prop.setter ? "true" : "false");
			buf << ",\n          \"getter_access\": \"" << access_level_name(prop.getter_access) << "\"";
			buf << ",\n          \"setter_access\": \"" << access_level_name(prop.setter_access) << "\"";
			buf << "\n        }";
		}
		buf << (info.properties.empty() ? "]" : "\n      ]");

		buf << ",\n      \"methods\": [";
		for (size_t i = 0; i < info.methods.size(); ++i) {
			const auto& method = info.methods[i];
			buf << (i ? ",\n        {\n" : "\n        {\n");
			buf << "          \"name\": ";
			write_json_string(buf, method.name.str());
			buf << ",\n          \"access\": \"" << access_level_name(method.access) << "\"";
			buf << ",\n          \"return_type\": \"" << variant_type_name(method.return_type) << "\"";
			buf << ",\n          \"param_types\": [";
			for (size_t p = 0; p < method.param_types.size(); ++p) {
				buf << (p ? ", " : "") << "\"" << variant_type_name(method.param_types[p]) << "\"";
			}
			buf << "]";
			buf << "\n        }";
		}
		buf << (info.methods.empty() ? "]" : "\n      ]");

		buf << "\n    }" << (ci + 1 < classes.size() ? ",\n" : "\n");
	}
	buf << "  ]\n}\n";

	if (path == "-") {
		std::cout << buf.str();
		return;
	}
	std::ofstream file(path);
	fassert(file.is_open(), std::format("dump_api_json: could not open '{}' for writing", path));
	file << buf.str();
}

Callable ClassDB::get_static_method(const StaticString& class_name, std::string_view func_name) {
	auto ci = _get_class_info_internal(class_name);
	if (!ci)
		return {};
	auto it = std::find_if(
			ci->methods.begin(), ci->methods.end(), [&func_name](ClassInfo::Method& m) { return m.name == func_name; });

	if (it != ci->methods.end()) {
		return it->callable;
	}
	return {};
}

} //namespace feather