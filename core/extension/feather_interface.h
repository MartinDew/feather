/* FeatherEngine plugin ABI. Pure C, stdbool.h/stddef.h/stdint.h only -- no
 * engine header may ever be included from here or from anything a plugin
 * includes. See docs/plugin_abi.md for the design rationale.
 *
 * Engine functions are resolved BY NAME via FeatherGetProcAddress, not
 * through a struct of function pointers, so adding one never changes a
 * struct layout and the interface version below almost never needs to bump.
 */
#ifndef FEATHER_EXTENSION_INTERFACE_H
#define FEATHER_EXTENSION_INTERFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FEATHER_INTERFACE_VERSION_MAJOR 1
#define FEATHER_INTERFACE_VERSION_MINOR 0

#if defined(_WIN32)
#define FEATHER_EXTENSION_EXPORT __declspec(dllexport)
#else
#define FEATHER_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

/* Opaque; a plugin never dereferences these, only passes them back. */
typedef void* FeatherObjectPtr;   /* feather::Reflected*        */
typedef void* FeatherClassPtr;    /* feather::ClassInfo*        */
typedef void* FeatherMethodPtr;   /* feather::ClassInfo::Method*  */
typedef void* FeatherVariantPtr;  /* feather::Variant*          */
typedef void* FeatherLibraryPtr;  /* identifies the calling plugin */

typedef void (*FeatherProc)(void);
typedef FeatherProc (*FeatherGetProcAddress)(const char* name);

/* Mirrors feather::VariantType -- kept numerically in sync by a
 * static_assert next to the engine-side enum. */
typedef enum {
	FEATHER_VARIANT_NIL = 0,
	FEATHER_VARIANT_BOOL,
	FEATHER_VARIANT_INT,
	FEATHER_VARIANT_FLOAT,
	FEATHER_VARIANT_VECTOR3,
	FEATHER_VARIANT_VECTOR2,
	FEATHER_VARIANT_VERTEX,
	FEATHER_VARIANT_COLOR,
	FEATHER_VARIANT_RID,
	FEATHER_VARIANT_STRING,
	FEATHER_VARIANT_ARRAY,
	FEATHER_VARIANT_PATH,
	FEATHER_VARIANT_OBJECT,
	FEATHER_VARIANT_INVALID
} FeatherVariantType;

/* Fixed C layouts for the POD VariantTypes -- the only ones method_ptrcall
 * can marshal directly. STRING/PATH/ARRAY have no fixed layout (they're
 * non-trivial C++ containers engine-side) and must go through
 * method_variant_call instead; see the comment on FeatherInterfaceMethodPtrcall. */
typedef struct {
	float x, y;
} FeatherVector2;

typedef struct {
	float x, y, z;
} FeatherVector3;

typedef struct {
	float r, g, b, a;
} FeatherColor;

typedef struct {
	FeatherVector3 position;
	FeatherVector3 normal;
	FeatherVector2 uv;
} FeatherVertex;

/* Borrowed view, UTF-8, valid only for the duration of the call it's passed
 * to -- never stored past it. */
typedef struct {
	const char* ptr;
	size_t len;
} FeatherStringRef;

typedef enum {
	FEATHER_INIT_CORE = 0,  /* fired when the extension is loaded, during index_project() */
	FEATHER_INIT_WORLD,     /* fired once WorldSim has a live flecs world -- not wired yet */
	FEATHER_INIT_MAX
} FeatherInitializationLevel;

typedef struct {
	uint32_t struct_size; /* sizeof(FeatherInitialization) as the PLUGIN saw it */
	void* userdata;
	void (*initialize)(void* userdata, FeatherInitializationLevel level);
	void (*deinitialize)(void* userdata, FeatherInitializationLevel level);
} FeatherInitialization;

/* The plugin's sole exported symbol, named "feather_extension_init". */
typedef bool (*FeatherInitializationFn)(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init);

/* ---- Functions available via FeatherGetProcAddress, by name ---- */

/* "feather_log" */
typedef void (*FeatherInterfaceLog)(const char* message);

/* "classdb_get_class" -- NULL if no such class is registered. */
typedef FeatherClassPtr (*FeatherInterfaceClassdbGetClass)(const char* class_name);

/* "classdb_class_get_method" -- NULL if the class has no such bound method. */
typedef FeatherMethodPtr (*FeatherInterfaceClassdbClassGetMethod)(FeatherClassPtr cls, const char* method_name);

/* "object_create" -- calls the class's reflection factory; NULL if the class
 * is abstract/singleton/value-type (no factory) or doesn't exist. */
typedef FeatherObjectPtr (*FeatherInterfaceObjectCreate)(const char* class_name);

/* "object_destroy" */
typedef void (*FeatherInterfaceObjectDestroy)(FeatherObjectPtr object);

/* "method_ptrcall" -- fast path. args[i] points at the FIXED C layout for
 * that parameter's declared FeatherVariantType (see the structs above);
 * `ret` likewise for the return type. Only valid when every parameter and
 * the return type is one of NIL/BOOL/INT/FLOAT/RID/OBJECT/VECTOR2/VECTOR3/
 * COLOR/VERTEX -- calling it on a method with a STRING/PATH/ARRAY in its
 * signature is a logic error (asserts in debug). Use method_variant_call
 * for those instead. `obj` is ignored for a static method. */
typedef void (*FeatherInterfaceMethodPtrcall)(
		FeatherMethodPtr method, FeatherObjectPtr obj, const void* const* args, void* ret);

/* "method_variant_call" -- always correct, for any bound method regardless
 * of signature; goes through feather::Variant on both sides. */
typedef void (*FeatherInterfaceMethodVariantCall)(FeatherMethodPtr method, FeatherObjectPtr obj,
		const FeatherVariantPtr* args, size_t argc, FeatherVariantPtr r_ret);

/* "object_get_property" / "object_set_property" -- returns false if no such
 * property exists or the accessor is missing (write-only / read-only
 * property) or its access level denies it. */
typedef bool (*FeatherInterfaceObjectGetProperty)(
		FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr r_out);
typedef bool (*FeatherInterfaceObjectSetProperty)(
		FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr value);

/* ---- Plugin-registered classes ----
 *
 * A plugin registers a new ClassDB entry under an existing extensible base;
 * the engine builds an internal C++ shim deriving from that base and
 * forwards its virtuals to the plugin through get_virtual_func, resolved
 * once at registration and cached. The plugin never allocates the shim --
 * create_instance returns the plugin's OWN opaque per-instance data (often
 * just class_userdata itself, or NULL for a stateless class); the engine
 * owns the shim's lifetime and passes that same pointer back into every
 * virtual call and into free_instance at teardown.
 *
 * Today the only extensible base is "ResourceFormatLoader" -- adding
 * another means adding another bridge (core/extension/bridges/), not
 * widening this struct. */

typedef void* (*FeatherClassCreateInstanceFn)(void* class_userdata, FeatherObjectPtr owner);
typedef void (*FeatherClassFreeInstanceFn)(void* class_userdata, void* instance);
/* Returns NULL for a virtual the plugin doesn't implement; the bridge then
 * falls back to the base class's default behavior where one exists (e.g.
 * ResourceFormatLoader::requires_immediate_load), or is a no-op/false. */
typedef FeatherProc (*FeatherClassGetVirtualFn)(void* class_userdata, const char* virtual_name);

typedef struct {
	uint32_t struct_size;
	void* class_userdata;
	FeatherClassCreateInstanceFn create_instance;
	FeatherClassFreeInstanceFn free_instance;
	FeatherClassGetVirtualFn get_virtual;
} FeatherExtensionClassInfo;

/* "classdb_register_extension_class" */
typedef bool (*FeatherInterfaceClassdbRegisterExtensionClass)(
		FeatherLibraryPtr library, const char* class_name, const char* parent_class_name,
		const FeatherExtensionClassInfo* info);

/* ---- ResourceFormatLoader virtuals, resolved via get_virtual by these names ---- */

/* "recognize_extension" */
typedef bool (*FeatherVirtualResourceFormatLoaderRecognizeExtension)(void* instance, const char* extension);
/* "instantiate" -- must return an already engine-allocated Resource (e.g.
 * via object_create); the engine always owns Resource lifetime, a plugin
 * never hands it a pointer it allocated itself. NULL means "decline". */
typedef FeatherObjectPtr (*FeatherVirtualResourceFormatLoaderInstantiate)(void* instance, const char* path_utf8);
/* "load" */
typedef void (*FeatherVirtualResourceFormatLoaderLoad)(void* instance, FeatherObjectPtr resource, const char* path_utf8);
/* "requires_immediate_load" -- optional; unimplemented means false. */
typedef bool (*FeatherVirtualResourceFormatLoaderRequiresImmediateLoad)(void* instance);

/* "variant_new" / "variant_destroy" -- every FeatherVariantPtr the plugin
 * holds must come from variant_new() and be freed with variant_destroy(). */
typedef FeatherVariantPtr (*FeatherInterfaceVariantNew)(void);
typedef void (*FeatherInterfaceVariantDestroy)(FeatherVariantPtr variant);
typedef FeatherVariantType (*FeatherInterfaceVariantGetType)(FeatherVariantPtr variant);

/* "variant_from_ptr" / "variant_to_ptr" -- convert between a Variant and the
 * fixed C layout for `type` (same restriction as method_ptrcall: not valid
 * for STRING/PATH/ARRAY). "variant_get_string_utf8" / "variant_set_string_utf8"
 * cover STRING/PATH instead (PATH is UTF-8 too, converted at the boundary). */
typedef void (*FeatherInterfaceVariantFromPtr)(FeatherVariantPtr dst, FeatherVariantType type, const void* src);
typedef bool (*FeatherInterfaceVariantToPtr)(FeatherVariantPtr src, FeatherVariantType type, void* dst);
typedef size_t (*FeatherInterfaceVariantGetStringUtf8)(FeatherVariantPtr variant, char* dst, size_t cap);
typedef void (*FeatherInterfaceVariantSetStringUtf8)(FeatherVariantPtr variant, const char* src, size_t len);

/* ---- ECS ----
 *
 * Deliberately not a general flecs mirror: only what a project needs to
 * declare components and run systems against the engine's one world.
 * Engine code never calls these -- it uses flecs directly (core/world/ecs_defs.h)
 * -- so there is exactly one ECS dialect per side of the boundary, not two
 * permanently-diverging ones. If a plugin needs relationships, prefabs,
 * observers, or custom query traversal, that's a signal the engine should
 * grow a first-class feature, not that this surface should widen. */

typedef uint64_t FeatherEntityId;
typedef uint64_t FeatherComponentId;
typedef void* FeatherWorldPtr; /* flecs ecs_world_t* */

typedef enum {
	FEATHER_PHASE_ON_UPDATE = 0 /* the only phase wired up so far */
} FeatherSystemPhase;

/* One call per matched TABLE, not per entity -- `columns[i]` is the base
 * pointer for term i's component array across all `count` entities in this
 * table, matching what flecs's own each()/iter() compile down to. Zero
 * engine calls needed in a system's inner loop. */
typedef struct {
	void* const* columns; /* one base pointer per term, columns[0..term_count) */
	const uint64_t* entities;
	int32_t count; /* entities in THIS table */
	int32_t term_count;
	float delta_time;
} FeatherTableIter;

typedef void (*FeatherSystemFn)(FeatherTableIter* iter);

/* "ecs_get_world" -- valid only from FEATHER_INIT_WORLD onward. */
typedef FeatherWorldPtr (*FeatherInterfaceEcsGetWorld)(void);

/* "ecs_register_component" */
typedef FeatherComponentId (*FeatherInterfaceEcsRegisterComponent)(
		FeatherWorldPtr world, const char* name, uint32_t size, uint32_t align);

/* "ecs_register_system" -- terms/term_count describe the query; `callback`
 * fires once per matched table for as long as the world exists (no
 * unregister yet, matching where plugin unload sits generally right now). */
typedef void (*FeatherInterfaceEcsRegisterSystem)(FeatherWorldPtr world, const char* name, FeatherSystemPhase phase,
		const FeatherComponentId* terms, int32_t term_count, FeatherSystemFn callback);

/* "ecs_entity_create" */
typedef FeatherEntityId (*FeatherInterfaceEcsEntityCreate)(FeatherWorldPtr world, const char* name);

/* "ecs_entity_set" -- adds the component if the entity doesn't have it yet,
 * then copies `size` bytes from `data` into it. `size` must match what the
 * component was registered with. */
typedef void (*FeatherInterfaceEcsEntitySet)(
		FeatherWorldPtr world, FeatherEntityId entity, FeatherComponentId component, const void* data, size_t size);

/* "ecs_entity_get_mut" -- NULL if the entity doesn't have the component. */
typedef void* (*FeatherInterfaceEcsEntityGetMut)(FeatherWorldPtr world, FeatherEntityId entity, FeatherComponentId component);

#ifdef __cplusplus
}
#endif

#endif /* FEATHER_EXTENSION_INTERFACE_H */
