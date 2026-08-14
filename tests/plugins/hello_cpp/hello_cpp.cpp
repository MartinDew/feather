// C++ test plugin exercising the generated bindings in feather_bindings.gen.h
// rather than the raw C ABI directly. Calls a real bound engine method
// (ResourceFormatLoader::recognize_extension) on a real engine object
// (MaterialFormatLoader) through generated code, going through the
// method_variant_call fallback since STRING has no fixed C layout.
#include <feather_bindings.gen.h>

#include <iostream>

static FeatherInterfaceLog log_fn = nullptr;

static void my_initialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	if (level != FEATHER_INIT_CORE)
		return;

	FeatherObjectPtr obj = feather::ext::_bindings::create("MaterialFormatLoader");
	if (!obj) {
		if (log_fn)
			log_fn("hello_cpp plugin: object_create('MaterialFormatLoader') failed");
		return;
	}

	feather::ext::ResourceFormatLoader loader(obj);
	bool matches_mat = loader.recognize_extension("mat");
	bool matches_txt = loader.recognize_extension("txt");

	if (log_fn) {
		log_fn(matches_mat ? "hello_cpp plugin: recognize_extension('mat') -> true (expected)"
							: "hello_cpp plugin: recognize_extension('mat') -> false (WRONG)");
		log_fn(matches_txt ? "hello_cpp plugin: recognize_extension('txt') -> true (WRONG)"
							: "hello_cpp plugin: recognize_extension('txt') -> false (expected)");
	}

	feather::ext::_bindings::destroy(obj);
}

static void my_deinitialize(void*, FeatherInitializationLevel) {}

extern "C" FEATHER_EXTENSION_EXPORT FeatherBool feather_extension_init(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init) {
	(void)library;

	log_fn = (FeatherInterfaceLog)get_proc_address("feather_log");
	feather::ext::_bindings::init(get_proc_address);

	if (log_fn)
		log_fn("hello_cpp plugin: feather_extension_init");

	r_init->struct_size = sizeof(FeatherInitialization);
	r_init->userdata = nullptr;
	r_init->initialize = my_initialize;
	r_init->deinitialize = my_deinitialize;
	return 1;
}
