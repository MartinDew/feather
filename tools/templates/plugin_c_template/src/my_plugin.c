// TODO: rename this file, the entry point, and the names in my_plugin.fext.

#include <feather_c/main/init_level.h>

#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

// The entry point named by my_plugin.fext. Called once per initialization level the engine enters, ascending; pick the
// earliest level that has what your registration needs. There is no matching call on the way out.
EXPORT void register_my_plugin(feather_InitLevel level) {
	if (level != feather_InitLevel_Core) {
		return;
	}

	printf("[my_plugin] hello from a C extension\n");
}
