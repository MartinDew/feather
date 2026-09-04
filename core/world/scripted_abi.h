#pragma once

#include <framework/export_defs.h>

#include <stddef.h>
#include <stdint.h>

// A flat C entry point for defining ECS types from a language that cannot call
// C++ at all.
//
// The C++ API this wraps (scripted_component.h, scripted_system.h) takes
// std::vector, std::function and World& -- fine for the embedded Python host,
// which is C++, and impossible for a NativeAOT C# assembly, which reaches the
// engine only through P/Invoke. Everything here is therefore plain C: pointers,
// integers, and one function pointer.
//
// Values cross as doubles, however many a field needs: one for bool, int and
// float, two for a vec2, three for a vec3, four for a color. That is uniform
// enough to need no per-type entry points, and lossless for every field type
// the engine can currently store (VariantType::INT is a 32-bit int).
//
// Field access goes through the component's registered accessors rather than a
// raw offset, so these work for the engine's own C++ components exactly as they
// do for scripted ones -- a caller can query Transform without knowing that it
// was not defined by a script.
//
// FEATHER_NO_BIND throughout: mrbind has no spelling for a function pointer,
// and these are hand-declared on the other side (the SDK's generated C#
// bootstrap) rather than generated.

extern "C" {

// Field types, numbered independently of VariantType so this ABI does not move
// when that enum does.
typedef enum FeatherScriptFieldType {
	FEATHER_SCRIPT_FIELD_BOOL = 0,
	FEATHER_SCRIPT_FIELD_INT = 1,
	FEATHER_SCRIPT_FIELD_FLOAT = 2,
	FEATHER_SCRIPT_FIELD_VEC2 = 3,
	FEATHER_SCRIPT_FIELD_VEC3 = 4,
	FEATHER_SCRIPT_FIELD_COLOR = 5,
} FeatherScriptFieldType;

// Pipeline phases, in the order they run.
typedef enum FeatherScriptPhase {
	FEATHER_SCRIPT_PHASE_ON_LOAD = 0,
	FEATHER_SCRIPT_PHASE_POST_LOAD = 1,
	FEATHER_SCRIPT_PHASE_PRE_UPDATE = 2,
	FEATHER_SCRIPT_PHASE_ON_UPDATE = 3,
	FEATHER_SCRIPT_PHASE_ON_VALIDATE = 4,
	FEATHER_SCRIPT_PHASE_POST_UPDATE = 5,
	FEATHER_SCRIPT_PHASE_PRE_STORE = 6,
	FEATHER_SCRIPT_PHASE_ON_STORE = 7,
} FeatherScriptPhase;

// Called once per matching entity. `components` has one entry per component the system queried, in the order they were named;
// each is the opaque handle the field accessors below take. Both it and the handles are valid only for the duration of the call.
typedef void (*FeatherScriptSystemFn)(
		void* user_data,
		uint64_t entity,
		void* const* components,
		int32_t component_count,
		double delta_time
);

// Returns the component id, or 0 on failure. On failure `error` receives a
// message (truncated to error_size, always null-terminated) when non-null.
FEATHER_NO_BIND FEATHER_C_ABI uint64_t feather_script_define_component(
		const char* name,
		int32_t field_count,
		const char* const* field_names,
		const uint8_t* field_types,
		char* error,
		int32_t error_size
);

// Returns the system id, or 0 on failure. `user_data` is handed back to the callback untouched and never freed (the engine cannot
// know what it is). The callback outlives the call and must remain valid for the process.
FEATHER_NO_BIND FEATHER_C_ABI uint64_t feather_script_define_system(
		const char* name,
		int32_t component_count,
		const char* const* component_names,
		uint8_t phase,
		FeatherScriptSystemFn callback,
		void* user_data,
		char* error,
		int32_t error_size
);

// Returns the entity id, or 0 on failure. An empty or null name makes an
// unnamed entity.
FEATHER_NO_BIND FEATHER_C_ABI uint64_t feather_script_create_entity(const char* name);

// Non-zero on success.
FEATHER_NO_BIND FEATHER_C_ABI int32_t feather_script_add_component(uint64_t entity, const char* component_name);

// A handle to one component of one entity, for use outside a system callback. Valid until the entity's archetype changes (adding or
// removing a component moves its storage), so it is meant to be used and dropped.
FEATHER_NO_BIND FEATHER_C_ABI void* feather_script_component_handle(uint64_t entity, const char* component_name);

// How many fields a component has, or -1 if it names no registered component.
FEATHER_NO_BIND FEATHER_C_ABI int32_t feather_script_field_count(const char* component_name);

// Describes one field. `out_name` receives a pointer to a string owned by the
// engine and valid for the process. Non-zero on success.
FEATHER_NO_BIND FEATHER_C_ABI int32_t feather_script_field_info(
		const char* component_name,
		int32_t index,
		const char** out_name,
		uint8_t* out_type
);

// Reads a field into `values`, writing how many doubles it used to
// `out_count`. Non-zero on success.
FEATHER_NO_BIND FEATHER_C_ABI int32_t feather_script_get_field(
		void* component_handle,
		const char* component_name,
		const char* field_name,
		double* values,
		int32_t max_values,
		int32_t* out_count
);

// Writes a field from `values`. Non-zero on success; fails if the count does
// not match what the field's type needs.
FEATHER_NO_BIND FEATHER_C_ABI int32_t feather_script_set_field(
		void* component_handle,
		const char* component_name,
		const char* field_name,
		const double* values,
		int32_t count
);

} // extern "C"
