#include "ecs_api.h"

#include <flecs.h>

namespace feather {
namespace ecs {

namespace {

// Heap-allocated, owned by the component via binding_ctx/binding_ctx_free
// (flecs calls the free callback when the component itself is deregistered).
// Holds just the four Feather hook pointers so the adapter trampolines below
// can forward flecs's real hook signature -- which carries an
// ecs_type_info_t* this API deliberately never exposes -- to Feather's
// simpler one.
struct HookContext {
	ComponentCtorFn ctor;
	ComponentDtorFn dtor;
	ComponentCopyFn copy;
	ComponentMoveFn move;
};

void adapt_ctor(void* ptr, int32_t count, const ecs_type_info_t* info) {
	static_cast<const HookContext*>(info->hooks.binding_ctx)->ctor(ptr, count);
}

void adapt_dtor(void* ptr, int32_t count, const ecs_type_info_t* info) {
	static_cast<const HookContext*>(info->hooks.binding_ctx)->dtor(ptr, count);
}

void adapt_copy(void* dst, const void* src, int32_t count, const ecs_type_info_t* info) {
	static_cast<const HookContext*>(info->hooks.binding_ctx)->copy(dst, src, count);
}

void adapt_move(void* dst, void* src, int32_t count, const ecs_type_info_t* info) {
	static_cast<const HookContext*>(info->hooks.binding_ctx)->move(dst, src, count);
}

void free_hook_context(void* ctx) {
	delete static_cast<HookContext*>(ctx);
}

ecs_world_t* raw_world(FeatherWorld world) {
	return static_cast<ecs_world_t*>(world._handle);
}

ecs_entity_t raw_entity(FeatherEntity e) {
	return static_cast<ecs_entity_t>(e.id);
}

ecs_world_t* entity_world(FeatherEntity e) {
	return static_cast<ecs_world_t*>(e.world);
}

// Every function here that resolves a bare NAME (ecs_entity_init/ecs_lookup
// for a component or system) is relative to flecs's CURRENT SCOPE
// (ecs_get_scope()/ecs_set_scope()) -- and flecs's own world.module<T>()
// changes that scope for the rest of the calling scope's lifetime.
// ecs_set_scope()'s own doc says restoring the old value is "considered good
// practice", NOT automatic, and it is NOT restored between two EcsFeature
// modules imported back to back from WorldSim::init()'s ClassDB loop:
// measured directly, a plain ecs_lookup(world, "Transform") returned a
// different, WRONG id depending on which module had most recently called
// world.module<T>(), even though the ecs_world_t* pointer was byte-identical
// both times. Every name-resolving function below forces root scope for the
// duration of the call via this guard, so this API's behavior never depends
// on scope state some earlier, unrelated caller left behind.
struct RootScopeGuard {
	ecs_world_t* world;
	ecs_entity_t prev;
	explicit RootScopeGuard(ecs_world_t* w) : world(w), prev(ecs_set_scope(w, 0)) {}
	~RootScopeGuard() { ecs_set_scope(world, prev); }
};

// Owned by the system via ecs_system_desc_t::ctx/ctx_free -- the system
// trampoline below reads it out of ecs_iter_t::ctx on every call.
struct SystemContext {
	FeatherSystemFn callback;
	int32_t term_count;
};

// FLECS_TERM_COUNT_MAX is flecs's own cap on terms per query (32 as of
// 4.1.5); mirrored here as a small fixed-size stack buffer for the column
// pointer array so building a FeatherIter never allocates.
constexpr int32_t kMaxSystemTerms = FLECS_TERM_COUNT_MAX;

void system_trampoline(ecs_iter_t* it) {
	auto* ctx = static_cast<SystemContext*>(it->ctx);
	void* columns[kMaxSystemTerms];
	int32_t n = ctx->term_count < kMaxSystemTerms ? ctx->term_count : kMaxSystemTerms;
	for (int32_t i = 0; i < n; ++i) {
		columns[i] = ecs_field_w_size(it, static_cast<size_t>(it->sizes[i]), static_cast<int8_t>(i));
	}
	FeatherIter fit {
		columns, it->entities, it->count, ctx->term_count,
		static_cast<float>(it->delta_time), it,
	};
	ctx->callback(&fit);
}

void free_system_context(void* ctx) {
	delete static_cast<SystemContext*>(ctx);
}

ecs_entity_t phase_entity(SystemPhase phase) {
	switch (phase) {
	case SystemPhase::PreUpdate: return EcsPreUpdate;
	case SystemPhase::OnUpdate: return EcsOnUpdate;
	case SystemPhase::PostUpdate: return EcsPostUpdate;
	case SystemPhase::PreStore: return EcsPreStore;
	case SystemPhase::OnStore: return EcsOnStore;
	}
	return EcsOnUpdate;
}

FeatherEntity wrap(ecs_world_t* w, ecs_entity_t e) {
	return FeatherEntity { e, w };
}

} // namespace

ComponentId register_component(FeatherWorld world, const ComponentDesc& desc) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = desc.name;
	ecs_entity_t entity = ecs_entity_init(w, &entity_desc);

	ecs_component_desc_t comp_desc {};
	comp_desc.entity = entity;
	comp_desc.type.size = static_cast<ecs_size_t>(desc.size);
	comp_desc.type.alignment = static_cast<ecs_size_t>(desc.align);

	// Idempotent re-registration (e.g. two independently-generated
	// register_<name>_components() calls both naming a component "Foo" --
	// legitimate if two loaded plugins happen to share a component name):
	// flecs only allows hooks to be assigned before a component has been
	// used, so attach them only the first time this name is seen. A
	// second call becomes a pure lookup; ecs_component_init itself already
	// verifies size/alignment against the existing registration and fails
	// loudly on a mismatch.
	if (ecs_get_type_info(w, entity) == nullptr) {
		auto* ctx = new HookContext { desc.ctor, desc.dtor, desc.copy, desc.move };
		comp_desc.type.hooks.ctor = adapt_ctor;
		comp_desc.type.hooks.dtor = adapt_dtor;
		comp_desc.type.hooks.copy = adapt_copy;
		comp_desc.type.hooks.move = adapt_move;
		comp_desc.type.hooks.binding_ctx = ctx;
		comp_desc.type.hooks.binding_ctx_free = free_hook_context;
	}

	return static_cast<ComponentId>(ecs_component_init(w, &comp_desc));
}

ComponentId lookup_component(FeatherWorld world, const char* name) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);
	return static_cast<ComponentId>(ecs_lookup(w, name));
}

void register_system(FeatherWorld world, const SystemDesc& desc) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = desc.name;
	ecs_entity_t entity = ecs_entity_init(w, &entity_desc);

	ecs_system_desc_t sys_desc {};
	sys_desc.entity = entity;
	sys_desc.phase = phase_entity(desc.phase);
	sys_desc.callback = system_trampoline;

	int32_t n = desc.term_count < kMaxSystemTerms ? desc.term_count : kMaxSystemTerms;
	for (int32_t i = 0; i < n; ++i) {
		sys_desc.query.terms[i].id = static_cast<ecs_id_t>(desc.terms[i]);
	}

	// Freed by flecs (via ctx_free) when the system itself is deregistered --
	// mirrors register_component()'s HookContext lifetime exactly.
	auto* ctx = new SystemContext { desc.callback, desc.term_count };
	sys_desc.ctx = ctx;
	sys_desc.ctx_free = free_system_context;

	ecs_system_init(w, &sys_desc);
}

FeatherEntity create_entity(FeatherWorld world, const char* name) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);
	ecs_entity_desc_t desc {};
	desc.name = name;
	return wrap(w, ecs_entity_init(w, &desc));
}

FeatherEntity create_child_entity(FeatherWorld world, FeatherEntity parent, const char* name) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);
	ecs_entity_desc_t desc {};
	desc.name = name;
	desc.parent = raw_entity(parent);
	return wrap(w, ecs_entity_init(w, &desc));
}

void mark_as_prefab(FeatherEntity e) {
	ecs_add_id(entity_world(e), raw_entity(e), EcsPrefab);
}

FeatherEntity instantiate_prefab(FeatherWorld world, FeatherEntity prefab, const char* name) {
	ecs_world_t* w = raw_world(world);
	RootScopeGuard _guard(w);
	ecs_entity_desc_t desc {};
	desc.name = name;
	ecs_entity_t inst = ecs_entity_init(w, &desc);
	ecs_add_id(w, inst, ecs_pair(EcsIsA, raw_entity(prefab)));
	return wrap(w, inst);
}

bool entity_is_valid(FeatherEntity e) {
	return e.is_valid() && ecs_is_alive(entity_world(e), raw_entity(e));
}

void destroy_entity(FeatherEntity e) {
	ecs_delete(entity_world(e), raw_entity(e));
}

void entity_child_of(FeatherEntity child, FeatherEntity parent) {
	ecs_add_id(entity_world(child), raw_entity(child), ecs_pair(EcsChildOf, raw_entity(parent)));
}

FeatherEntity entity_parent(FeatherEntity e) {
	ecs_world_t* w = entity_world(e);
	ecs_entity_t target = ecs_get_target(w, raw_entity(e), EcsChildOf, 0);
	// target == 0 when there is none; wrap() still round-trips it correctly
	// since FeatherEntity{0, w}.is_valid() is false, same contract as a
	// default-constructed FeatherEntity.
	return wrap(w, target);
}

void entity_add(FeatherEntity e, ComponentId comp) {
	ecs_add_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp));
}

void entity_remove(FeatherEntity e, ComponentId comp) {
	ecs_remove_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp));
}

bool entity_has(FeatherEntity e, ComponentId comp) {
	return ecs_has_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp));
}

const void* entity_get_raw(FeatherEntity e, ComponentId comp) {
	return ecs_get_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp));
}

void* entity_get_mut_raw(FeatherEntity e, ComponentId comp) {
	return ecs_get_mut_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp));
}

void entity_set_raw(FeatherEntity e, ComponentId comp, const void* data, size_t size) {
	ecs_set_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp), size, data);
}

void* entity_emplace_raw(FeatherEntity e, ComponentId comp, size_t size, bool* is_new) {
	return ecs_emplace_id(entity_world(e), raw_entity(e), static_cast<ecs_id_t>(comp), size, is_new);
}

} // namespace ecs
} // namespace feather
