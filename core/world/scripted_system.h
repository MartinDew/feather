#pragma once

#include "ecs_defs.h"

#include <framework/class_info.h>

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace feather {

// Where in the frame a scripted system runs. Mirrors flecs' built-in pipeline
// phases; named here so a script never has to name a flecs entity.
enum class ScriptedSystemPhase : uint8_t {
	OnLoad,
	PostLoad,
	PreUpdate,
	OnUpdate,
	OnValidate,
	PostUpdate,
	PreStore,
	OnStore,
};

// One component matched by a scripted system, as seen from the callback.
//
// The pointer is into the ECS's own storage, so writing through the accessors
// writes the entity's actual component -- there is no copy to send back. The
// accessors come from the component's ClassInfo, which is why this works for a
// component defined by a script and one defined in C++ alike: both register a
// value class whose property accessors take the raw component pointer.
struct ScriptedSystemComponent {
	const ClassInfo* info = nullptr;
	void* data = nullptr;
};

// One entity's worth of work.
struct ScriptedSystemInvocation {
	Ecs::entity_t entity = 0;
	// One entry per queried component, in the order they were requested.
	std::span<const ScriptedSystemComponent> components;
	double delta_time = 0.0;
};

// Called once per matched entity.
using ScriptedSystemCallback = std::function<void(const ScriptedSystemInvocation&)>;

// Registers a system defined at runtime, over components named at runtime.
//
// The components may be scripted (see scripted_component.h) or ordinary C++
// value-type components: both are looked up by the name they registered under,
// and both expose their fields through the same ClassInfo accessors.
//
// Returns the flecs system id, or 0 on failure, writing the reason to *error
// when given one.
Ecs::entity_t register_scripted_system(
		World& world,
		const std::string& name,
		const std::vector<std::string>& component_names,
		ScriptedSystemPhase phase,
		ScriptedSystemCallback callback,
		std::string* error = nullptr
);

} //namespace feather
