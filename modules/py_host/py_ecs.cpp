// The bridge a Python script uses to define ECS types.
//
// An embedded module rather than part of the generated `feather` module: the
// registration API takes a callable and hands back raw component storage,
// neither of which mrbind can express (see the --ignore entries in
// xmake/modules/feather_bindings.lua). It is deliberately small and low-level;
// the decorators a script actually writes against live in the Python shim
// shipped beside it (python/feather_ecs.py).
#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include "py_host.h"

#include <main/class_db.h>
#include <main/world_sim.h>
#include <world/scripted_component.h>
#include <world/scripted_system.h>

#include <deque>
#include <string>
#include <vector>

namespace py = pybind11;

namespace feather {

namespace {

VariantType variant_type_from_name(const std::string& name) {
	if (name == "bool") return VariantType::BOOL;
	if (name == "int") return VariantType::INT;
	if (name == "float") return VariantType::FLOAT;
	if (name == "vec2") return VariantType::VECTOR2;
	if (name == "vec3") return VariantType::VECTOR3;
	if (name == "color") return VariantType::COLOR;
	return VariantType::INVALID;
}

ScriptedSystemPhase phase_from_name(const std::string& name) {
	if (name == "on_load") return ScriptedSystemPhase::OnLoad;
	if (name == "post_load") return ScriptedSystemPhase::PostLoad;
	if (name == "pre_update") return ScriptedSystemPhase::PreUpdate;
	if (name == "on_update") return ScriptedSystemPhase::OnUpdate;
	if (name == "on_validate") return ScriptedSystemPhase::OnValidate;
	if (name == "post_update") return ScriptedSystemPhase::PostUpdate;
	if (name == "pre_store") return ScriptedSystemPhase::PreStore;
	if (name == "on_store") return ScriptedSystemPhase::OnStore;
	throw py::value_error("unknown phase '" + name + "'");
}

// Vectors and colors cross as tuples rather than as the `feather` module's
// bound types: that module is built separately and dlopened, so its registered
// types are not visible from this one, and a tuple needs no shared registry.
py::object variant_to_py(const Variant& value) {
	switch (value.get_type()) {
		case VariantType::BOOL:
			return py::cast(value.as<bool>().value_or(false));
		case VariantType::INT:
			return py::cast(value.as<int>().value_or(0));
		case VariantType::FLOAT:
			return py::cast(static_cast<double>(value.as<real_t>().value_or(0)));
		case VariantType::VECTOR2: {
			auto v = value.as<Vector2>().value_or(Vector2 {});
			return py::make_tuple(v.x, v.y);
		}
		case VariantType::VECTOR3: {
			auto v = value.as<Vector3>().value_or(Vector3 {});
			return py::make_tuple(v.x, v.y, v.z);
		}
		case VariantType::COLOR: {
			auto c = value.as<Color>().value_or(Color {});
			return py::make_tuple(c.x, c.y, c.z, c.w);
		}
		default:
			return py::none();
	}
}

Variant py_to_variant(const py::handle& value, VariantType type) {
	switch (type) {
		case VariantType::BOOL:
			return Variant(value.cast<bool>());
		case VariantType::INT:
			return Variant(value.cast<int>());
		case VariantType::FLOAT:
			return Variant(static_cast<real_t>(value.cast<double>()));
		case VariantType::VECTOR2: {
			auto t = value.cast<std::vector<double>>();
			if (t.size() != 2) throw py::value_error("expected 2 numbers for a vec2");
			return Variant(Vector2(static_cast<float>(t[0]), static_cast<float>(t[1])));
		}
		case VariantType::VECTOR3: {
			auto t = value.cast<std::vector<double>>();
			if (t.size() != 3) throw py::value_error("expected 3 numbers for a vec3");
			return Variant(Vector3(static_cast<float>(t[0]), static_cast<float>(t[1]), static_cast<float>(t[2])));
		}
		case VariantType::COLOR: {
			auto t = value.cast<std::vector<double>>();
			if (t.size() != 4) throw py::value_error("expected 4 numbers for a color");
			return Variant(Color(
					static_cast<float>(t[0]), static_cast<float>(t[1]),
					static_cast<float>(t[2]), static_cast<float>(t[3])
			));
		}
		default:
			throw py::type_error("field type cannot be written from Python");
	}
}

// A view onto one component of one entity, valid only for the duration of the
// system callback it was handed to. Attribute access goes through the
// component's registered properties, so this works the same for a component
// defined by a script and one defined in C++.
struct ComponentView {
	const ClassInfo* info = nullptr;
	void* data = nullptr;

	const ClassInfo::Property* find(const std::string& name) const {
		for (const auto& property : info->properties) {
			if (property.name.str() == name) {
				return &property;
			}
		}
		return nullptr;
	}

	py::object get(const std::string& name) const {
		if (!data) {
			throw py::attribute_error("this component view is no longer valid");
		}
		const auto* property = find(name);
		if (!property || !property->getter) {
			throw py::attribute_error("'" + std::string(info->name.str()) + "' has no field '" + name + "'");
		}
		return variant_to_py(property->getter(data));
	}

	void set(const std::string& name, const py::object& value) {
		if (!data) {
			throw py::attribute_error("this component view is no longer valid");
		}
		const auto* property = find(name);
		if (!property || !property->setter) {
			throw py::attribute_error("'" + std::string(info->name.str()) + "' has no writable field '" + name + "'");
		}
		property->setter(data, py_to_variant(value, property->type));
	}

	std::vector<std::string> field_names() const {
		std::vector<std::string> names;
		names.reserve(info->properties.size());
		for (const auto& property : info->properties) {
			names.emplace_back(property.name.str());
		}
		return names;
	}
};

World& script_world() {
	WorldSim* sim = WorldSim::get();
	if (!sim) {
		throw std::runtime_error("no world is available yet");
	}
	return *sim->get_world();
}

// Callbacks outlive the process, deliberately.
//
// A Python object may only be released while the interpreter is alive, and
// nothing here runs before Py_Finalize: flecs frees a system's context when the
// world is torn down, and a static container would be destroyed at exit -- both
// after the interpreter is gone. Either one is a segfault in _Py_Dealloc.
//
// So this is allocated once and never freed. The cost is one function object
// per system for the life of the process; the alternative is ordering teardown
// against an interpreter that is already unavailable.
std::deque<py::function>& retained_callbacks() {
	static auto* callbacks = new std::deque<py::function>();
	return *callbacks;
}

} //namespace

// See the declaration in py_host.h: this exists so the linker keeps this file.
void py_ecs_link_anchor() {}

PYBIND11_EMBEDDED_MODULE(_feather_ecs, m) {
	m.doc() = "Low-level ECS registration for Feather scripts; see feather_ecs.py";

	py::class_<ComponentView>(m, "ComponentView")
			.def("__getattr__", &ComponentView::get)
			.def("__setattr__", &ComponentView::set)
			.def("field_names", &ComponentView::field_names)
			.def("__repr__", [](const ComponentView& view) {
				return "<ComponentView " + std::string(view.info ? view.info->name.str() : "?") + ">";
			});

	m.def(
			"define_component",
			[](const std::string& name, const std::vector<std::pair<std::string, std::string>>& fields) {
				std::vector<ScriptedField> resolved;
				resolved.reserve(fields.size());
				for (const auto& [field_name, type_name] : fields) {
					const VariantType type = variant_type_from_name(type_name);
					if (type == VariantType::INVALID) {
						throw py::value_error(
								"field '" + field_name + "' has unsupported type '" + type_name
								+ "' (use bool, int, float, vec2, vec3 or color)"
						);
					}
					resolved.push_back(ScriptedField { field_name, type });
				}

				std::string error;
				const auto component = register_scripted_component(script_world(), name, resolved, &error);
				if (!component) {
					throw std::runtime_error(error);
				}
				return static_cast<uint64_t>(component);
			},
			py::arg("name"), py::arg("fields")
	);

	m.def(
			"create_entity",
			[](const std::string& name) {
				World& world = script_world();
				auto entity = name.empty() ? world.entity() : world.entity(name.c_str());
				return static_cast<uint64_t>(entity.id());
			},
			py::arg("name") = std::string {}
	);

	m.def(
			"add_component",
			[](uint64_t entity, const std::string& component_name) {
				World& world = script_world();
				const auto component = world.lookup(component_name.c_str());
				if (!component.is_valid()) {
					throw py::value_error("no component named '" + component_name + "'");
				}
				ecs_add_id(world.c_ptr(), entity, component);
			},
			py::arg("entity"), py::arg("component")
	);

	// Field access outside a system callback.
	//
	// The pointer is good until the entity's archetype changes -- adding or
	// removing a component moves its storage -- so this is meant to be used and
	// dropped, not kept. Inside a system, the views handed to the callback are
	// the ones to use.
	m.def(
			"component_view",
			[](uint64_t entity, const std::string& component_name) {
				World& world = script_world();
				const auto component = world.lookup(component_name.c_str());
				if (!component.is_valid()) {
					throw py::value_error("no component named '" + component_name + "'");
				}

				const ecs_type_info_t* type_info = ecs_get_type_info(world.c_ptr(), component);
				if (!type_info || type_info->size <= 0) {
					throw py::value_error("'" + component_name + "' is a tag, not a component with fields");
				}
				if (!ecs_has_id(world.c_ptr(), entity, component)) {
					throw py::value_error("that entity does not have '" + component_name + "'");
				}

				const ClassInfo* info = ClassDB::get_class_info(component_name);
				if (!info) {
					throw py::value_error("'" + component_name + "' has no registered class");
				}

				ComponentView view;
				view.info = info;
				view.data = ecs_ensure_id(world.c_ptr(), entity, component, static_cast<size_t>(type_info->size));
				return view;
			},
			py::arg("entity"), py::arg("component")
	);

	m.def(
			"define_system",
			[](const std::string& name,
			   const std::vector<std::string>& components,
			   const std::string& phase,
			   py::function callback) {
				retained_callbacks().push_back(std::move(callback));
				py::function* retained = &retained_callbacks().back();

				std::string error;
				const auto system = register_scripted_system(
						script_world(), name, components, phase_from_name(phase),
						[retained](const ScriptedSystemInvocation& invocation) {
							// The pipeline runs on the thread that initialized
							// the interpreter, but the callback may raise, and a
							// Python exception must not cross back into flecs.
							py::gil_scoped_acquire gil;

							py::list views;
							for (const auto& component : invocation.components) {
								ComponentView view;
								view.info = component.info;
								view.data = component.data;
								views.append(py::cast(view));
							}

							try {
								(*retained)(
										static_cast<uint64_t>(invocation.entity), views, invocation.delta_time
								);
							}
							catch (const py::error_already_set& e) {
								PyErr_Clear();
								py::print("feather: error in system callback:", e.what());
							}

							// The storage these point at is only this frame's;
							// a script that kept one gets a clear error rather
							// than a dangling write.
							for (auto view : views) {
								view.cast<ComponentView&>().data = nullptr;
							}
						},
						&error
				);
				if (!system) {
					throw std::runtime_error(error);
				}
				return static_cast<uint64_t>(system);
			},
			py::arg("name"), py::arg("components"), py::arg("phase"), py::arg("callback")
	);
}

} //namespace feather
