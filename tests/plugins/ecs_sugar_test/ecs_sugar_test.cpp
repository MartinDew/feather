// C++ test plugin for feather::ecs (the typed sugar generated on top of
// the six raw ecs_* functions). Registers TWO independent systems over the
// SAME component type -- the scenario that would silently misbehave without
// FeatherTableIter::user_data, since both systems would otherwise share the
// one trampoline instantiated for that type.
#include <feather_bindings.gen.h>

namespace {

struct Counter {
	int value;
};

int a_ticks = 0;
int b_ticks = 0;

void system_a(Counter& c, float) {
	(void)c;
	++a_ticks;
	if (a_ticks == 1)
		feather::_bindings::log("ecs_sugar_test plugin: system_a ticked");
}

void system_b(Counter& c, float) {
	(void)c;
	++b_ticks;
	if (b_ticks == 1)
		feather::_bindings::log("ecs_sugar_test plugin: system_b ticked");
}

void initialize(void*, FeatherInitializationLevel level) {
	if (level != FEATHER_INIT_WORLD)
		return;

	feather::ecs::register_component<Counter>("Counter");
	feather::ecs::each<Counter>("SystemA", FEATHER_PHASE_ON_UPDATE, &system_a);
	feather::ecs::each<Counter>("SystemB", FEATHER_PHASE_ON_UPDATE, &system_b);
	feather::ecs::spawn<Counter>("counter_entity", Counter { 0 });

	feather::_bindings::log("ecs_sugar_test plugin: registered two systems over the same component type");
}

void deinitialize(void*, FeatherInitializationLevel) {}

} // namespace

extern "C" FEATHER_EXTENSION_EXPORT bool feather_extension_init(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init) {
	(void)library;

	feather::_bindings::init(get_proc_address);
	feather::_bindings::log("ecs_sugar_test plugin: feather_extension_init");

	r_init->struct_size = sizeof(FeatherInitialization);
	r_init->userdata = nullptr;
	r_init->initialize = &initialize;
	r_init->deinitialize = &deinitialize;
	return true;
}
