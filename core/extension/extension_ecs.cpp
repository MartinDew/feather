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
	void* user_data;
};

void system_trampoline(ecs_iter_t* it) {
	auto* ctx = static_cast<SystemContext*>(it->ctx);

	// ecs_field_w_size's `index` is 0-based (verified against flecs's own
	// assertion `index < it->field_count`, despite a doc comment example
	// that suggests otherwise for a different, higher-level API).
	//
	// it->sizes[i] == 0 covers two cases that both need a NULL column rather
	// than a real call: a genuine tag (zero-sized component, nothing to
	// fetch) and an optional term absent from this table (flecs still
	// reports its registered size as 0 in that slot) -- ecs_field_w_size
	// itself asserts size != 0, so either case would crash if called.
	std::vector<void*> columns(static_cast<size_t>(ctx->term_count));
	for (int32_t i = 0; i < ctx->term_count; ++i) {
		columns[i] = it->sizes[i] != 0 ? ecs_field_w_size(it, static_cast<size_t>(it->sizes[i]), static_cast<int8_t>(i))
										: nullptr;
	}

	FeatherTableIter titer {};
	titer.columns = columns.data();
	titer.entities = it->entities;
	titer.count = it->count;
	titer.term_count = ctx->term_count;
	titer.delta_time = static_cast<float>(it->delta_time);
	titer.user_data = ctx->user_data;
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

ecs_entity_t feather_phase_id(FeatherSystemPhase phase) {
	switch (phase) {
	case FEATHER_PHASE_PRE_UPDATE:
		return EcsPreUpdate;
	case FEATHER_PHASE_ON_UPDATE:
		return EcsOnUpdate;
	case FEATHER_PHASE_POST_UPDATE:
		return EcsPostUpdate;
	case FEATHER_PHASE_PRE_STORE:
		return EcsPreStore;
	case FEATHER_PHASE_ON_STORE:
		return EcsOnStore;
	}
	return EcsOnUpdate;
}

void feather_ecs_register_system(FeatherWorldPtr world, const FeatherSystemDesc* desc_in) {
	auto* w = static_cast<ecs_world_t*>(world);

	// Leaked deliberately -- there's no system unregistration path yet, same
	// as ClassDB extension classes (see feather_interface.cpp's `intern`).
	auto* ctx = new SystemContext { desc_in->callback, desc_in->term_count, desc_in->user_data };

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = desc_in->name;

	ecs_system_desc_t desc {};
	desc.entity = ecs_entity_init(w, &entity_desc);
	for (int32_t i = 0; i < desc_in->term_count && i < FLECS_TERM_COUNT_MAX; ++i) {
		const FeatherSystemTerm& src_term = desc_in->terms[i];
		ecs_term_t& term = desc.query.terms[i];
		term.id = static_cast<ecs_id_t>(src_term.id);
		if (src_term.flags & FEATHER_TERM_CONST)
			term.inout = EcsIn;
		if (src_term.flags & FEATHER_TERM_OPTIONAL)
			term.oper = EcsOptional;
		if (src_term.flags & FEATHER_TERM_UP)
			term.src.id |= EcsUp;
	}
	desc.callback = &system_trampoline;
	desc.ctx = ctx;
	desc.phase = feather_phase_id(desc_in->phase);
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
