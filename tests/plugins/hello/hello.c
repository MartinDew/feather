/* Hand-written C test plugin for the feather_extension_init handshake --
 * intentionally has no build system of its own (see the compile line in
 * docs/plugin_abi.md's Stage 3 verification) to prove the ABI needs neither
 * C++ nor a matching compiler/stdlib on the plugin side. */
#include <extension/feather_interface.h>
#include <stdio.h>
#include <string.h>

static FeatherInterfaceLog log_fn = 0;
static FeatherInterfaceClassdbGetClass classdb_get_class_fn = 0;
static FeatherInterfaceClassdbRegisterExtensionClass register_class_fn = 0;

static FeatherBool hello_recognize_extension(void* instance, const char* extension) {
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

static void my_initialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	if (level != FEATHER_INIT_CORE)
		return;

	FeatherClassPtr resource_class = classdb_get_class_fn ? classdb_get_class_fn("Resource") : 0;

	if (log_fn) {
		log_fn(resource_class ? "hello plugin: found class 'Resource'" : "hello plugin: 'Resource' NOT FOUND");
	}

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

FEATHER_EXTENSION_EXPORT FeatherBool feather_extension_init(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init) {
	(void)library;

	log_fn = (FeatherInterfaceLog)get_proc_address("feather_log");
	classdb_get_class_fn = (FeatherInterfaceClassdbGetClass)get_proc_address("classdb_get_class");
	register_class_fn = (FeatherInterfaceClassdbRegisterExtensionClass)get_proc_address("classdb_register_extension_class");

	if (log_fn)
		log_fn("hello plugin: feather_extension_init");

	r_init->struct_size = sizeof(FeatherInitialization);
	r_init->userdata = 0;
	r_init->initialize = my_initialize;
	r_init->deinitialize = my_deinitialize;
	return 1;
}
