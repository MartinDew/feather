#include "scripted_abi.h"

#include "scripted_component.h"
#include "scripted_system.h"

#include <main/class_db.h>
#include <main/world_sim.h>

#include <cstring>
#include <deque>
#include <string>

namespace feather {

namespace {

void write_error(char* error, int32_t error_size, const std::string& message) {
	if (!error || error_size <= 0) {
		return;
	}
	const size_t room = static_cast<size_t>(error_size) - 1;
	const size_t length = std::min(room, message.size());
	std::memcpy(error, message.data(), length);
	error[length] = '\0';
}

World* world_or_null() {
	WorldSim* sim = WorldSim::get();
	return sim ? sim->get_world() : nullptr;
}

VariantType variant_type_from_abi(uint8_t type) {
	switch (static_cast<FeatherScriptFieldType>(type)) {
		case FEATHER_SCRIPT_FIELD_BOOL: return VariantType::BOOL;
		case FEATHER_SCRIPT_FIELD_INT: return VariantType::INT;
		case FEATHER_SCRIPT_FIELD_FLOAT: return VariantType::FLOAT;
		case FEATHER_SCRIPT_FIELD_VEC2: return VariantType::VECTOR2;
		case FEATHER_SCRIPT_FIELD_VEC3: return VariantType::VECTOR3;
		case FEATHER_SCRIPT_FIELD_COLOR: return VariantType::COLOR;
	}
	return VariantType::INVALID;
}

// -1 for a type this ABI does not carry.
int32_t abi_type_from_variant(VariantType type) {
	switch (type) {
		case VariantType::BOOL: return FEATHER_SCRIPT_FIELD_BOOL;
		case VariantType::INT: return FEATHER_SCRIPT_FIELD_INT;
		case VariantType::FLOAT: return FEATHER_SCRIPT_FIELD_FLOAT;
		case VariantType::VECTOR2: return FEATHER_SCRIPT_FIELD_VEC2;
		case VariantType::VECTOR3: return FEATHER_SCRIPT_FIELD_VEC3;
		case VariantType::COLOR: return FEATHER_SCRIPT_FIELD_COLOR;
		default: return -1;
	}
}

int32_t value_count_for(VariantType type) {
	switch (type) {
		case VariantType::BOOL:
		case VariantType::INT:
		case VariantType::FLOAT: return 1;
		case VariantType::VECTOR2: return 2;
		case VariantType::VECTOR3: return 3;
		case VariantType::COLOR: return 4;
		default: return 0;
	}
}

const ClassInfo::Property* find_property(const char* component_name, const char* field_name) {
	if (!component_name || !field_name) {
		return nullptr;
	}
	const ClassInfo* info = ClassDB::get_class_info(component_name);
	if (!info) {
		return nullptr;
	}
	for (const ClassInfo::Property& property : info->properties) {
		if (property.name.str() == field_name) {
			return &property;
		}
	}
	return nullptr;
}

Variant variant_from_values(VariantType type, const double* values, int32_t count) {
	switch (type) {
		case VariantType::BOOL: return Variant(values[0] != 0.0);
		case VariantType::INT: return Variant(static_cast<int>(values[0]));
		case VariantType::FLOAT: return Variant(static_cast<real_t>(values[0]));
		case VariantType::VECTOR2:
			return Variant(Vector2(static_cast<float>(values[0]), static_cast<float>(values[1])));
		case VariantType::VECTOR3:
			return Variant(Vector3(
					static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])
			));
		case VariantType::COLOR:
			return Variant(Color(
					static_cast<float>(values[0]), static_cast<float>(values[1]),
					static_cast<float>(values[2]), static_cast<float>(values[3])
			));
		default: break;
	}
	(void)count;
	return Variant();
}

int32_t values_from_variant(const Variant& value, double* values, int32_t max_values) {
	const int32_t needed = value_count_for(value.get_type());
	if (needed == 0 || needed > max_values) {
		return 0;
	}
	switch (value.get_type()) {
		case VariantType::BOOL:
			values[0] = value.as<bool>().value_or(false) ? 1.0 : 0.0;
			break;
		case VariantType::INT:
			values[0] = static_cast<double>(value.as<int>().value_or(0));
			break;
		case VariantType::FLOAT:
			values[0] = static_cast<double>(value.as<real_t>().value_or(0));
			break;
		case VariantType::VECTOR2: {
			auto v = value.as<Vector2>().value_or(Vector2 {});
			values[0] = v.x;
			values[1] = v.y;
			break;
		}
		case VariantType::VECTOR3: {
			auto v = value.as<Vector3>().value_or(Vector3 {});
			values[0] = v.x;
			values[1] = v.y;
			values[2] = v.z;
			break;
		}
		case VariantType::COLOR: {
			auto c = value.as<Color>().value_or(Color {});
			values[0] = c.x;
			values[1] = c.y;
			values[2] = c.z;
			values[3] = c.w;
			break;
		}
		default:
			return 0;
	}
	return needed;
}

// Field names handed out through the ABI must outlive the call, and StaticString
// does not promise a stable char* for the caller to keep. These do.
const char* stable_name(const std::string& name) {
	static auto* names = new std::deque<std::string>();
	for (const std::string& existing : *names) {
		if (existing == name) {
			return existing.c_str();
		}
	}
	names->push_back(name);
	return names->back().c_str();
}

} //namespace

} //namespace feather

using namespace feather;

extern "C" {

uint64_t feather_script_define_component(
		const char* name,
		int32_t field_count,
		const char* const* field_names,
		const uint8_t* field_types,
		char* error,
		int32_t error_size
) {
	World* world = world_or_null();
	if (!world) {
		write_error(error, error_size, "no world is available yet");
		return 0;
	}
	if (!name || field_count <= 0 || !field_names || !field_types) {
		write_error(error, error_size, "a component needs a name and at least one field");
		return 0;
	}

	std::vector<ScriptedField> fields;
	fields.reserve(static_cast<size_t>(field_count));
	for (int32_t i = 0; i < field_count; i++) {
		const VariantType type = variant_type_from_abi(field_types[i]);
		if (type == VariantType::INVALID || !field_names[i]) {
			write_error(error, error_size, std::string("field ") + std::to_string(i) + " has an unsupported type");
			return 0;
		}
		fields.push_back(ScriptedField { field_names[i], type });
	}

	std::string message;
	const auto component = register_scripted_component(*world, name, fields, &message);
	if (!component) {
		write_error(error, error_size, message);
	}
	return static_cast<uint64_t>(component);
}

uint64_t feather_script_define_system(
		const char* name,
		int32_t component_count,
		const char* const* component_names,
		uint8_t phase,
		FeatherScriptSystemFn callback,
		void* user_data,
		char* error,
		int32_t error_size
) {
	World* world = world_or_null();
	if (!world) {
		write_error(error, error_size, "no world is available yet");
		return 0;
	}
	if (!name || !callback || component_count <= 0 || !component_names) {
		write_error(error, error_size, "a system needs a name, a callback and at least one component");
		return 0;
	}
	if (phase > FEATHER_SCRIPT_PHASE_ON_STORE) {
		write_error(error, error_size, "unknown phase");
		return 0;
	}

	std::vector<std::string> components;
	components.reserve(static_cast<size_t>(component_count));
	for (int32_t i = 0; i < component_count; i++) {
		if (!component_names[i]) {
			write_error(error, error_size, "a queried component name is null");
			return 0;
		}
		components.emplace_back(component_names[i]);
	}

	std::string message;
	const auto system = register_scripted_system(
			*world, name, components, static_cast<ScriptedSystemPhase>(phase),
			[callback, user_data](const ScriptedSystemInvocation& invocation) {
				// The handles are the raw component pointers; the accessors the
				// caller will use to read them are looked up by name on the
				// other side of the ABI.
				std::vector<void*> handles;
				handles.reserve(invocation.components.size());
				for (const ScriptedSystemComponent& component : invocation.components) {
					handles.push_back(component.data);
				}
				callback(
						user_data, static_cast<uint64_t>(invocation.entity), handles.data(),
						static_cast<int32_t>(handles.size()), invocation.delta_time
				);
			},
			&message
	);
	if (!system) {
		write_error(error, error_size, message);
	}
	return static_cast<uint64_t>(system);
}

uint64_t feather_script_create_entity(const char* name) {
	World* world = world_or_null();
	if (!world) {
		return 0;
	}
	auto entity = (name && *name) ? world->entity(name) : world->entity();
	return static_cast<uint64_t>(entity.id());
}

int32_t feather_script_add_component(uint64_t entity, const char* component_name) {
	World* world = world_or_null();
	if (!world || !component_name) {
		return 0;
	}
	const auto component = world->lookup(component_name);
	if (!component.is_valid()) {
		return 0;
	}
	ecs_add_id(world->c_ptr(), entity, component);
	return 1;
}

void* feather_script_component_handle(uint64_t entity, const char* component_name) {
	World* world = world_or_null();
	if (!world || !component_name) {
		return nullptr;
	}
	const auto component = world->lookup(component_name);
	if (!component.is_valid()) {
		return nullptr;
	}
	const ecs_type_info_t* type_info = ecs_get_type_info(world->c_ptr(), component);
	if (!type_info || type_info->size <= 0 || !ecs_has_id(world->c_ptr(), entity, component)) {
		return nullptr;
	}
	return ecs_ensure_id(world->c_ptr(), entity, component, static_cast<size_t>(type_info->size));
}

int32_t feather_script_field_count(const char* component_name) {
	if (!component_name) {
		return -1;
	}
	const ClassInfo* info = ClassDB::get_class_info(component_name);
	if (!info) {
		return -1;
	}
	return static_cast<int32_t>(info->properties.size());
}

int32_t feather_script_field_info(
		const char* component_name,
		int32_t index,
		const char** out_name,
		uint8_t* out_type
) {
	if (!component_name || index < 0) {
		return 0;
	}
	const ClassInfo* info = ClassDB::get_class_info(component_name);
	if (!info || index >= static_cast<int32_t>(info->properties.size())) {
		return 0;
	}

	const ClassInfo::Property& property = info->properties[static_cast<size_t>(index)];
	const int32_t type = abi_type_from_variant(property.type);
	if (type < 0) {
		// A field this ABI cannot carry -- a C++ component may well have one.
		return 0;
	}

	if (out_name) {
		*out_name = stable_name(std::string(property.name.str()));
	}
	if (out_type) {
		*out_type = static_cast<uint8_t>(type);
	}
	return 1;
}

int32_t feather_script_get_field(
		void* component_handle,
		const char* component_name,
		const char* field_name,
		double* values,
		int32_t max_values,
		int32_t* out_count
) {
	if (!component_handle || !values) {
		return 0;
	}
	const ClassInfo::Property* property = find_property(component_name, field_name);
	if (!property || !property->getter) {
		return 0;
	}

	const int32_t written = values_from_variant(property->getter(component_handle), values, max_values);
	if (written == 0) {
		return 0;
	}
	if (out_count) {
		*out_count = written;
	}
	return 1;
}

int32_t feather_script_set_field(
		void* component_handle,
		const char* component_name,
		const char* field_name,
		const double* values,
		int32_t count
) {
	if (!component_handle || !values) {
		return 0;
	}
	const ClassInfo::Property* property = find_property(component_name, field_name);
	if (!property || !property->setter) {
		return 0;
	}
	if (count != value_count_for(property->type)) {
		return 0;
	}

	property->setter(component_handle, variant_from_values(property->type, values, count));
	return 1;
}

} // extern "C"
