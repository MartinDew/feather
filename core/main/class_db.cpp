#include "class_db.h"
#include "framework/reflected.h"
#include <algorithm>
#include <memory>
#include <ranges>

#include <print>

namespace feather {

FSINGLETON_INSTANCE(ClassDB);

ClassDB::ClassDB() {
	FSINGLETON_CONSTRUCT_INSTANCE()
	_class_infos.insert(std::make_pair("Reflected", ClassInfo { .name = "Reflected"_ss, .parent = ""_ss }));
}

ClassDB::~ClassDB() {
	clear();
}

void ClassDB::clear() {
	_instance->_class_infos.clear();
	_instance->_subclass_delegates.clear();
	_instance->_current_info = nullptr;
}

Reflected* ClassDB::create_object_unsafe(std::string_view name) {
	auto object_info_it = _instance->_class_infos.find(name);
	if (object_info_it != _instance->_class_infos.end()) {
		return object_info_it->second.object_create_func().as<Reflected*>().value();
	}

	return {};
}

bool ClassDB::register_scripted_value_class(StaticString name, std::vector<ClassInfo::Property> properties) {
	ClassDB& instance = *get();

	if (instance._class_infos.contains(name)) {
		std::println("ClassDB: '{}' is already registered; ignoring the scripted definition", name.str());
		return false;
	}

	std::println("Registering class '{}' as {} object", name.str(), "scripted value type");

	ClassInfo& info = instance._class_infos[name];
	info.name = name;
	// Deliberately parentless: a scripted value type derives from nothing, so there is no base whose ClassInfo* would have
	// to outlive it, and _fire_subclass_delegates has no chain to walk.
	info.parent = ""_ss;
	info.is_abstract = false;
	info.is_singleton = false;
	info.is_value_type = true;
	info.object_create_func = nullptr;
	info.properties = std::move(properties);

	// No _current_info dance: that exists so a generated _bind_members() can
	// find the entry it is populating. Here the members arrive with the call.
	_fire_subclass_delegates(name);
	return true;
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

const ClassInfo* ClassDB::get_class_info(std::string_view class_name) {
	return _get_class_info_internal(class_name);
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

Delegate<std::string_view>::id_t ClassDB::on_subclass_registered(
		std::string_view base_class_name,
		const Delegate<std::string_view>::DelegateFuncType& callback
) {
	return get()->_subclass_delegates[StaticString(base_class_name)].subscribe(callback);
}

void ClassDB::unregister_subclass_delegate(std::string_view base_class_name, Delegate<std::string_view>::id_t id) {
	auto it = get()->_subclass_delegates.find(StaticString(base_class_name));
	if (it != get()->_subclass_delegates.end()) {
		it->second.remove(id);
	}
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

		std::println("Children : {}", get_children_names_string(name, false));
	}
#endif
}

Callable ClassDB::get_static_method(const StaticString& class_name, std::string_view func_name) {
	auto ci = _get_class_info_internal(class_name);
	if (!ci)
		return {};
	auto it = std::find_if(ci->methods.begin(), ci->methods.end(), [&func_name](ClassInfo::Method& m) {
		return m.name == func_name;
	});

	if (it != ci->methods.end()) {
		return it->callable;
	}
	return {};
}

} //namespace feather