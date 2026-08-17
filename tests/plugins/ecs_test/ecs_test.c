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
static int logged_readonly = 0;

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

/* Read-only term (FEATHER_TERM_CONST) on a different phase (PreStore, not
 * the default OnUpdate) -- exercises both without touching the mutation
 * spinner_system already proves. */
static void spinner_readonly_system(FeatherTableIter* it) {
	const Spinner* spinners = (const Spinner*)it->columns[0];
	if (!logged_readonly && it->count > 0 && spinners[0].speed >= 0.0f) {
		logged_readonly = 1;
		if (log_fn)
			log_fn("ecs_test plugin: read-only PreStore system observed Spinner");
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

	FeatherSystemTerm terms[1];
	terms[0].id = spinner_id;
	terms[0].flags = FEATHER_TERM_NONE;

	FeatherSystemDesc desc;
	desc.struct_size = sizeof(FeatherSystemDesc);
	desc.name = "SpinnerSystem";
	desc.phase = FEATHER_PHASE_ON_UPDATE;
	desc.terms = terms;
	desc.term_count = 1;
	desc.callback = &spinner_system;
	desc.user_data = 0;
	ecs_register_system_fn(world, &desc);

	FeatherSystemTerm readonly_terms[1];
	readonly_terms[0].id = spinner_id;
	readonly_terms[0].flags = FEATHER_TERM_CONST;

	FeatherSystemDesc readonly_desc;
	readonly_desc.struct_size = sizeof(FeatherSystemDesc);
	readonly_desc.name = "SpinnerReadonlySystem";
	readonly_desc.phase = FEATHER_PHASE_PRE_STORE;
	readonly_desc.terms = readonly_terms;
	readonly_desc.term_count = 1;
	readonly_desc.callback = &spinner_readonly_system;
	readonly_desc.user_data = 0;
	ecs_register_system_fn(world, &readonly_desc);

	if (log_fn)
		log_fn("ecs_test plugin: registered Spinner component, entity, and two systems");
}

static void my_deinitialize(void* userdata, FeatherInitializationLevel level) {
	(void)userdata;
	(void)level;
}

FEATHER_EXTENSION_EXPORT bool feather_extension_init(
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
	return true;
}
