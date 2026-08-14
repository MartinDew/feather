/* Hand-written C test plugin for the ECS surface: declares a component,
 * creates an entity with it, registers a system that mutates it every tick,
 * and logs once when the mutation is first observed. */
#include <extension/feather_interface.h>
#include <stdio.h>

static FeatherInterfaceLog log_fn = 0;
static FeatherInterfaceEcsGetWorld ecs_get_world_fn = 0;
static FeatherInterfaceEcsRegisterComponent ecs_register_component_fn = 0;
static FeatherInterfaceEcsRegisterSystem ecs_register_system_fn = 0;
static FeatherInterfaceEcsEntityCreate ecs_entity_create_fn = 0;
static FeatherInterfaceEcsEntitySet ecs_entity_set_fn = 0;

typedef struct {
	float speed;
} Spinner;

static int logged_change = 0;

static void spinner_system(FeatherTableIter* it) {
	Spinner* spinners = (Spinner*)it->columns[0];
	int i;
	for (i = 0; i < it->count; ++i) {
		float before = spinners[i].speed;
		spinners[i].speed += it->delta_time;
		if (!logged_change && before == 0.0f && spinners[i].speed > 0.0f) {
			logged_change = 1;
			if (log_fn)
				log_fn("ecs_test plugin: system observed Spinner.speed change from 0");
		}
	}
}

static void my_initialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	if (level != FEATHER_INIT_WORLD)
		return;

	FeatherWorldPtr world = ecs_get_world_fn();
	FeatherComponentId spinner_id = ecs_register_component_fn(world, "Spinner", (uint32_t)sizeof(Spinner), (uint32_t)_Alignof(Spinner));

	Spinner initial;
	initial.speed = 0.0f;
	FeatherEntityId entity = ecs_entity_create_fn(world, "spin_test_entity");
	ecs_entity_set_fn(world, entity, spinner_id, &initial, sizeof(Spinner));

	FeatherComponentId terms[1];
	terms[0] = spinner_id;
	ecs_register_system_fn(world, "SpinnerSystem", FEATHER_PHASE_ON_UPDATE, terms, 1, &spinner_system);

	if (log_fn)
		log_fn("ecs_test plugin: registered Spinner component, entity, and system");
}

static void my_deinitialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	(void)level;
}

FEATHER_EXTENSION_EXPORT FeatherBool feather_extension_init(
		FeatherGetProcAddress get_proc_address, FeatherLibraryPtr library, FeatherInitialization* r_init) {
	(void)library;

	log_fn = (FeatherInterfaceLog)get_proc_address("feather_log");
	ecs_get_world_fn = (FeatherInterfaceEcsGetWorld)get_proc_address("ecs_get_world");
	ecs_register_component_fn = (FeatherInterfaceEcsRegisterComponent)get_proc_address("ecs_register_component");
	ecs_register_system_fn = (FeatherInterfaceEcsRegisterSystem)get_proc_address("ecs_register_system");
	ecs_entity_create_fn = (FeatherInterfaceEcsEntityCreate)get_proc_address("ecs_entity_create");
	ecs_entity_set_fn = (FeatherInterfaceEcsEntitySet)get_proc_address("ecs_entity_set");

	if (log_fn)
		log_fn("ecs_test plugin: feather_extension_init");

	r_init->struct_size = sizeof(FeatherInitialization);
	r_init->userdata = 0;
	r_init->initialize = my_initialize;
	r_init->deinitialize = my_deinitialize;
	return 1;
}
