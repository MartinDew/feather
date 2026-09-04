#pragma once

#include "ecs_defs.h"

#include <framework/class_info.h>
#include <framework/variant.h>

#include <string>
#include <vector>

namespace feather {

// One field of a component type defined at runtime. Only Variant-compatible scalar and math types are allowed, and only the trivially
// copyable ones: the component's storage is raw memory flecs zero-initializes with no destructor, ruling out STRING/ARRAY/PATH/OBJECT for now.
struct ScriptedField {
	std::string name;
	VariantType type = VariantType::INVALID;
};

// Where a field ended up, and how to read and write it.
struct ScriptedFieldLayout {
	std::string name;
	VariantType type = VariantType::INVALID;
	size_t offset = 0;
	std::function<Variant(void*)> getter;
	std::function<void(void*, Variant)> setter;
};

// The resolved layout of a scripted component. Kept by the engine rather than derived from ClassDB on demand, because the consumer
// that needs it most is the system trampoline: it converts raw ECS storage to Variants once per matched entity, not a name lookup.
struct ScriptedComponentLayout {
	std::string name;
	Ecs::entity_t component = 0;
	size_t size = 0;
	size_t alignment = 1;
	std::vector<ScriptedFieldLayout> fields;
};

// Registers a component type described at runtime rather than by a C++ type: flecs gets a component sized/aligned here, and ClassDB
// gets a value class whose properties read/write the fields at those offsets, so reflection sees them like an FSTRUCT's. Returns the flecs id, or 0 with *error set -- a normal outcome, since the description comes from outside the engine.
Ecs::entity_t register_scripted_component(
		World& world,
		const std::string& name,
		const std::vector<ScriptedField>& fields,
		std::string* error = nullptr
);

// The layout registered for a component id, or nullptr if it names no scripted component. Pointers stay valid for the life of the
// process: layouts are only ever added, since a registered component cannot be withdrawn from a world that may already store it.
const ScriptedComponentLayout* find_scripted_component(Ecs::entity_t component);

// The same, by the name the script registered.
const ScriptedComponentLayout* find_scripted_component(const std::string& name);

} //namespace feather
