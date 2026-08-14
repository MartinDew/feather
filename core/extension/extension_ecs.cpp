#include "feather_interface.h"

#include "extension_interface.h"
#include <main/world_sim.h>

#include <flecs.h>

#include <string_view>
#include <vector>

using namespace feather;

// Not extern "C": these implement the "ecs_*" ABI names, but flecs's own C
// API already defines a real ecs_get_world() (and could add others clashing
// by name later) -- extern "C" linkage would collide with it at link time.
// Nothing dlsym's these directly; PROC_TABLE hands out their address by
// pointer, so ordinary (mangled) C++ linkage is all that's needed.
namespace {

struct SystemContext {
	FeatherSystemFn callback;
	int32_t term_count;
};

void system_trampoline(ecs_iter_t* it) {
	auto* ctx = static_cast<SystemContext*>(it->ctx);

	// ecs_field_w_size's `index` is 0-based (verified against flecs's own
	// assertion `index < it->field_count`, despite a doc comment example
	// that suggests otherwise for a different, higher-level API).
	std::vector<void*> columns(static_cast<size_t>(ctx->term_count));
	for (int32_t i = 0; i < ctx->term_count; ++i) {
		columns[i] = ecs_field_w_size(it, static_cast<size_t>(it->sizes[i]), static_cast<int8_t>(i));
	}

	FeatherTableIter titer {};
	titer.columns = columns.data();
	titer.entities = it->entities;
	titer.count = it->count;
	titer.term_count = ctx->term_count;
	titer.delta_time = static_cast<float>(it->delta_time);
	ctx->callback(&titer);
}

FeatherWorldPtr feather_ecs_get_world() {
	return WorldSim::get()->get_world()->c_ptr();
}

FeatherComponentId feather_ecs_register_component(FeatherWorldPtr world, const char* name, uint32_t size, uint32_t align) {
	auto* w = static_cast<ecs_world_t*>(world);

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = name;
	ecs_entity_t entity = ecs_entity_init(w, &entity_desc);

	ecs_component_desc_t comp_desc {};
	comp_desc.entity = entity;
	comp_desc.type.size = static_cast<ecs_size_t>(size);
	comp_desc.type.alignment = static_cast<ecs_size_t>(align);
	return static_cast<FeatherComponentId>(ecs_component_init(w, &comp_desc));
}

void feather_ecs_register_system(FeatherWorldPtr world, const char* name, FeatherSystemPhase phase,
		const FeatherComponentId* terms, int32_t term_count, FeatherSystemFn callback) {
	auto* w = static_cast<ecs_world_t*>(world);

	// Leaked deliberately -- there's no system unregistration path yet, same
	// as ClassDB extension classes (see feather_interface.cpp's `intern`).
	auto* ctx = new SystemContext { callback, term_count };

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = name;

	ecs_system_desc_t desc {};
	desc.entity = ecs_entity_init(w, &entity_desc);
	for (int32_t i = 0; i < term_count && i < FLECS_TERM_COUNT_MAX; ++i) {
		desc.query.terms[i].id = static_cast<ecs_id_t>(terms[i]);
	}
	desc.callback = &system_trampoline;
	desc.ctx = ctx;
	switch (phase) {
	case FEATHER_PHASE_ON_UPDATE:
	default:
		desc.phase = EcsOnUpdate;
		break;
	}
	ecs_system_init(w, &desc);
}

FeatherEntityId feather_ecs_entity_create(FeatherWorldPtr world, const char* name) {
	auto* w = static_cast<ecs_world_t*>(world);
	ecs_entity_desc_t desc {};
	desc.name = name;
	return static_cast<FeatherEntityId>(ecs_entity_init(w, &desc));
}

void feather_ecs_entity_set(
		FeatherWorldPtr world, FeatherEntityId entity, FeatherComponentId component, const void* data, size_t size) {
	auto* w = static_cast<ecs_world_t*>(world);
	ecs_set_id(w, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component), size, data);
}

void* feather_ecs_entity_get_mut(FeatherWorldPtr world, FeatherEntityId entity, FeatherComponentId component) {
	auto* w = static_cast<ecs_world_t*>(world);
	return ecs_get_mut_id(w, static_cast<ecs_entity_t>(entity), static_cast<ecs_id_t>(component));
}

} // namespace

namespace feather {

FeatherProc feather_get_ecs_proc_address(const char* name) {
	std::string_view needle(name);
	if (needle == "ecs_get_world")
		return reinterpret_cast<FeatherProc>(&feather_ecs_get_world);
	if (needle == "ecs_register_component")
		return reinterpret_cast<FeatherProc>(&feather_ecs_register_component);
	if (needle == "ecs_register_system")
		return reinterpret_cast<FeatherProc>(&feather_ecs_register_system);
	if (needle == "ecs_entity_create")
		return reinterpret_cast<FeatherProc>(&feather_ecs_entity_create);
	if (needle == "ecs_entity_set")
		return reinterpret_cast<FeatherProc>(&feather_ecs_entity_set);
	if (needle == "ecs_entity_get_mut")
		return reinterpret_cast<FeatherProc>(&feather_ecs_entity_get_mut);
	return nullptr;
}
} // namespace feather
