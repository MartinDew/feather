/* FeatherEngine plugin ABI. Pure C, stdint.h/stddef.h only -- no engine
 * header may ever be included from here or from anything a plugin includes.
 * See docs/plugin_abi.md for the design rationale.
 *
 * Engine functions are resolved BY NAME via FeatherGetProcAddress, not
 * through a struct of function pointers, so adding one never changes a
 * struct layout and the interface version below almost never needs to bump.
 */
#ifndef FEATHER_EXTENSION_INTERFACE_H
#define FEATHER_EXTENSION_INTERFACE_H

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

typedef uint8_t FeatherBool;

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
typedef FeatherBool (*FeatherInitializationFn)(
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

/* "object_get_property" / "object_set_property" -- FeatherBool return is
 * false if no such property exists or the accessor is missing (write-only /
 * read-only property) or its access level denies it. */
typedef FeatherBool (*FeatherInterfaceObjectGetProperty)(
		FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr r_out);
typedef FeatherBool (*FeatherInterfaceObjectSetProperty)(
		FeatherObjectPtr obj, FeatherClassPtr cls, const char* prop_name, FeatherVariantPtr value);

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
typedef FeatherBool (*FeatherInterfaceVariantToPtr)(FeatherVariantPtr src, FeatherVariantType type, void* dst);
typedef size_t (*FeatherInterfaceVariantGetStringUtf8)(FeatherVariantPtr variant, char* dst, size_t cap);
typedef void (*FeatherInterfaceVariantSetStringUtf8)(FeatherVariantPtr variant, const char* src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* FEATHER_EXTENSION_INTERFACE_H */
