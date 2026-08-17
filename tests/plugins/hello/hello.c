/* Hand-written C test plugin for the feather_extension_init handshake and
 * classdb_register_extension_class. Intentionally has no build system of its
 * own -- compiled directly with `cc -shared -fPIC` -- to prove the ABI needs
 * neither C++ nor a matching compiler/stdlib on the plugin side. */
#include <extension/feather_interface.h>
#include <stdio.h>
#include <string.h>

static FeatherInterfaceLog log_fn = 0;
static FeatherInterfaceClassdbGetClass classdb_get_class_fn = 0;
static FeatherInterfaceClassdbRegisterExtensionClass register_class_fn = 0;
static FeatherInterfaceClassdbGetSingleton classdb_get_singleton_fn = 0;
static FeatherInterfaceClassdbClassGetSize classdb_class_get_size_fn = 0;
static FeatherInterfaceClassdbClassGetAlign classdb_class_get_align_fn = 0;
static FeatherInterfaceClassdbClassGetMethod classdb_class_get_method_fn = 0;
static FeatherInterfaceObjectCreate object_create_fn = 0;
static FeatherInterfaceObjectDestroy object_destroy_fn = 0;
static FeatherInterfaceMethodVariantCall method_variant_call_fn = 0;
static FeatherInterfaceVariantNew variant_new_fn = 0;
static FeatherInterfaceVariantDestroy variant_destroy_fn = 0;
static FeatherInterfaceVariantFromPtr variant_from_ptr_fn = 0;
static FeatherInterfaceVariantToPtr variant_to_ptr_fn = 0;
static FeatherInterfaceVariantFromArray variant_from_array_fn = 0;
static FeatherInterfaceVariantToArray variant_to_array_fn = 0;
static FeatherInterfaceArrayNew array_new_fn = 0;
static FeatherInterfaceArrayDestroy array_destroy_fn = 0;
static FeatherInterfaceArrayAppend array_append_fn = 0;
static FeatherInterfaceArraySize array_size_fn = 0;
static FeatherInterfaceArrayGet array_get_fn = 0;

static bool hello_recognize_extension(void* instance, const char* extension) {
	(void)instance;
	if (log_fn)
		log_fn("hello plugin: recognize_extension() called");
	return extension && extension[0] == 't' && extension[1] == 'x' && extension[2] == 't' && extension[3] == '\0';
}

static FeatherProc hello_loader_get_virtual(void* class_userdata, const char* name) {
	(void)class_userdata;
	if (!strcmp(name, "recognize_extension"))
		return (FeatherProc)&hello_recognize_extension;
	return 0;
}

/* ResourceLoader is a safe singleton to probe here: it's a Main member,
 * constructed before setup_db() even runs, unlike WorldSim (an Engine
 * member -- not guaranteed live this early). classdb_get_singleton would
 * fassert (abort the process) on a singleton class whose instance doesn't
 * exist yet. */
static void test_singleton_and_size(void) {
	if (classdb_get_singleton_fn) {
		FeatherObjectPtr loader = classdb_get_singleton_fn("ResourceLoader");
		if (log_fn)
			log_fn(loader ? "hello plugin: classdb_get_singleton('ResourceLoader') -> found"
						  : "hello plugin: classdb_get_singleton('ResourceLoader') -> NOT FOUND");
	}
	if (classdb_get_class_fn && classdb_class_get_size_fn && classdb_class_get_align_fn) {
		FeatherClassPtr texture_class = classdb_get_class_fn("Texture");
		uint32_t size = texture_class ? classdb_class_get_size_fn(texture_class) : 0;
		uint32_t align = texture_class ? classdb_class_get_align_fn(texture_class) : 0;
		if (log_fn)
			log_fn(size > 0 && align > 0 ? "hello plugin: classdb_class_get_size/align('Texture') -> nonzero"
										  : "hello plugin: classdb_class_get_size/align('Texture') -> ZERO");
	}
}

/* Exercises the Array ABI (only path for a method with ARRAY in its
 * signature -- see docs/plugin_abi.md) round-trip through
 * ComplexMesh::add_vertices/get_vertices. */
static void test_array_roundtrip(void) {
	if (!classdb_get_class_fn || !object_create_fn || !classdb_class_get_method_fn || !method_variant_call_fn ||
			!variant_new_fn || !array_new_fn || !array_append_fn || !variant_from_ptr_fn || !variant_from_array_fn ||
			!variant_to_array_fn || !array_size_fn || !array_get_fn)
		return;

	FeatherClassPtr mesh_class = classdb_get_class_fn("ComplexMesh");
	FeatherObjectPtr mesh = mesh_class ? object_create_fn("ComplexMesh") : 0;
	if (!mesh) {
		if (log_fn)
			log_fn("hello plugin: array roundtrip -> object_create('ComplexMesh') FAILED");
		return;
	}

	FeatherArrayPtr verts = array_new_fn();
	FeatherVertex v = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } };
	FeatherVariantPtr vertex_variant = variant_new_fn();
	variant_from_ptr_fn(vertex_variant, FEATHER_VARIANT_VERTEX, &v);
	array_append_fn(verts, vertex_variant);
	v.position.x = 1.0f;
	variant_from_ptr_fn(vertex_variant, FEATHER_VARIANT_VERTEX, &v);
	array_append_fn(verts, vertex_variant);
	variant_destroy_fn(vertex_variant);

	FeatherVariantPtr array_arg = variant_new_fn();
	variant_from_array_fn(array_arg, verts);
	array_destroy_fn(verts);

	FeatherMethodPtr add_vertices = classdb_class_get_method_fn(mesh_class, "add_vertices");
	if (add_vertices) {
		FeatherVariantPtr args[1] = { array_arg };
		method_variant_call_fn(add_vertices, mesh, args, 1, 0);
	}
	variant_destroy_fn(array_arg);

	FeatherMethodPtr get_vertices = classdb_class_get_method_fn(mesh_class, "get_vertices");
	int roundtrip_ok = 0;
	if (get_vertices) {
		FeatherVariantPtr ret = variant_new_fn();
		method_variant_call_fn(get_vertices, mesh, 0, 0, ret);
		FeatherArrayPtr result = array_new_fn();
		if (variant_to_array_fn(ret, result) && array_size_fn(result) == 2) {
			FeatherVariantPtr elem = variant_new_fn();
			if (array_get_fn(result, 1, elem)) {
				FeatherVertex out;
				roundtrip_ok = variant_to_ptr_fn(elem, FEATHER_VARIANT_VERTEX, &out) && out.position.x == 1.0f;
			}
			variant_destroy_fn(elem);
		}
		array_destroy_fn(result);
		variant_destroy_fn(ret);
	}

	if (log_fn)
		log_fn(roundtrip_ok ? "hello plugin: array roundtrip through ComplexMesh -> OK"
							 : "hello plugin: array roundtrip through ComplexMesh -> FAILED");

	if (object_destroy_fn)
		object_destroy_fn(mesh);
}

static void my_initialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	if (level != FEATHER_INIT_CORE)
		return;

	FeatherClassPtr resource_class = classdb_get_class_fn ? classdb_get_class_fn("Resource") : 0;

	if (log_fn) {
		log_fn(resource_class ? "hello plugin: found class 'Resource'" : "hello plugin: 'Resource' NOT FOUND");
	}

	test_singleton_and_size();
	test_array_roundtrip();

	if (register_class_fn) {
		FeatherExtensionClassInfo info;
		info.struct_size = sizeof(FeatherExtensionClassInfo);
		info.class_userdata = 0;
		info.create_instance = 0; /* stateless -- no per-instance data needed */
		info.free_instance = 0;
		info.get_virtual = &hello_loader_get_virtual;
		register_class_fn(0, "HelloFormatLoader", "ResourceFormatLoader", &info);
		if (log_fn)
			log_fn("hello plugin: registered HelloFormatLoader");
	}
}

static void my_deinitialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	(void)level;
	if (log_fn)
		log_fn("hello plugin: deinitialize");
}

FEATHER_EXTENSION_EXPORT bool feather_extension_init(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init) {
	(void)library;

	log_fn = (FeatherInterfaceLog)get_proc_address("feather_log");
	classdb_get_class_fn = (FeatherInterfaceClassdbGetClass)get_proc_address("classdb_get_class");
	register_class_fn = (FeatherInterfaceClassdbRegisterExtensionClass)get_proc_address("classdb_register_extension_class");
	classdb_get_singleton_fn = (FeatherInterfaceClassdbGetSingleton)get_proc_address("classdb_get_singleton");
	classdb_class_get_size_fn = (FeatherInterfaceClassdbClassGetSize)get_proc_address("classdb_class_get_size");
	classdb_class_get_align_fn = (FeatherInterfaceClassdbClassGetAlign)get_proc_address("classdb_class_get_align");
	classdb_class_get_method_fn = (FeatherInterfaceClassdbClassGetMethod)get_proc_address("classdb_class_get_method");
	object_create_fn = (FeatherInterfaceObjectCreate)get_proc_address("object_create");
	object_destroy_fn = (FeatherInterfaceObjectDestroy)get_proc_address("object_destroy");
	method_variant_call_fn = (FeatherInterfaceMethodVariantCall)get_proc_address("method_variant_call");
	variant_new_fn = (FeatherInterfaceVariantNew)get_proc_address("variant_new");
	variant_destroy_fn = (FeatherInterfaceVariantDestroy)get_proc_address("variant_destroy");
	variant_from_ptr_fn = (FeatherInterfaceVariantFromPtr)get_proc_address("variant_from_ptr");
	variant_to_ptr_fn = (FeatherInterfaceVariantToPtr)get_proc_address("variant_to_ptr");
	variant_from_array_fn = (FeatherInterfaceVariantFromArray)get_proc_address("variant_from_array");
	variant_to_array_fn = (FeatherInterfaceVariantToArray)get_proc_address("variant_to_array");
	array_new_fn = (FeatherInterfaceArrayNew)get_proc_address("array_new");
	array_destroy_fn = (FeatherInterfaceArrayDestroy)get_proc_address("array_destroy");
	array_append_fn = (FeatherInterfaceArrayAppend)get_proc_address("array_append");
	array_size_fn = (FeatherInterfaceArraySize)get_proc_address("array_size");
	array_get_fn = (FeatherInterfaceArrayGet)get_proc_address("array_get");

	if (log_fn)
		log_fn("hello plugin: feather_extension_init");

	r_init->struct_size = sizeof(FeatherInitialization);
	r_init->userdata = 0;
	r_init->initialize = my_initialize;
	r_init->deinitialize = my_deinitialize;
	return true;
}
