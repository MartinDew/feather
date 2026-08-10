#pragma once

// The plugin-facing ECS surface. This header carries ZERO flecs dependency
// (no #include <flecs.h>, no forward-declared flecs type) so a plugin can
// register components/systems and manipulate entities without flecs ever
// entering its dynamic symbol table -- see the plugin-abi-rework plan's "ECS
// abstraction" section for the full rationale. flecs's C++ layer largely
// duplicates reflection FeatherEngine already has (RTTI-based type-name
// parsing, a per-binary id cache) -- generate_reflection.py already knows a
// component's name at build time, so this API takes it as an explicit string
// instead of re-deriving it at runtime.
//
// Engine-internal code that needs richer flecs features (relationships with
// .up() traversal, raw flecs::iter access, multi_threaded(), custom phases
// beyond OnUpdate) is NOT required to route through this header -- it can
// keep using <world/ecs_defs.h> (flecs::world/entity) directly, exactly as
// core/world/rendering_world_feature.cpp does today. The firewall is only at
// the plugin boundary; this is the boundary.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace feather {
namespace ecs {

// Opaque wrapper around a flecs::world's raw ecs_world_t*. Never dereferenced
// by plugin code -- only ecs_api.cpp (which links flecs) knows what's really
// behind the pointer. WorldSim hands one to every EcsFeature's
// _import_module(WorldSim*) via WorldSim::ecs_world().
struct WorldHandle {
	void* _handle = nullptr;
};

// Opaque entity handle: an id plus the raw world it belongs to. This is
// deliberately NOT feather::Entity (flecs::entity) -- flecs::entity's methods
// (.set<T>(), .emplace<T>(), .add<T>()) are templates that call ecs_* C
// functions directly from the CALLING translation unit, which is exactly
// what would pull flecs into a plugin's undefined-symbol table. Trivially
// cheap to construct/copy: two machine words, no allocation, no vtable.
struct EntityHandle {
	uint64_t id = 0;
	void* world = nullptr;

	[[nodiscard]] bool is_valid() const { return id != 0; }
};

using ComponentId = uint64_t;

// Batch hook signatures, matching flecs's own ecs_xtor_t/ecs_copy_t/ecs_move_t
// minus the ecs_type_info_t* parameter (which would require flecs.h to name).
// ecs_api.cpp adapts these to the real flecs hook signatures via a
// binding_ctx pointer -- see register_component()'s implementation.
using ComponentCtorFn = void (*)(void* ptr, int32_t count);
using ComponentDtorFn = void (*)(void* ptr, int32_t count);
using ComponentCopyFn = void (*)(void* dst, const void* src, int32_t count);
using ComponentMoveFn = void (*)(void* dst, void* src, int32_t count);

struct ComponentDesc {
	const char* name; // static storage in the caller; ecs_api never frees it
	size_t size;
	size_t align;
	ComponentCtorFn ctor;
	ComponentDtorFn dtor;
	ComponentCopyFn copy;
	ComponentMoveFn move;
};

// Registers (or, if `name` already names a component in this world -- e.g.
// because the engine registered it first, as with Transform -- finds) a
// component and returns its id. Implemented via flecs's plain C API
// (ecs_entity_init + ecs_component_init) rather than the C++ template layer,
// so no RTTI/type-name parsing happens at all: `name` is the single source
// of component identity, matching what generate_reflection.py already knows
// statically.
ComponentId register_component(WorldHandle world, const ComponentDesc& desc);

// Resolves a component already registered under `name` (by this project, the
// engine, or another loaded plugin) without re-registering it. Used by a
// system that references a component it didn't itself register -- e.g. a
// plugin's OnUpdate system touching the engine's own Transform.
ComponentId lookup_component(WorldHandle world, const char* name);

// ---------------------------------------------------------------------------
// Systems and iteration.
//
// flecs's ecs_field_w_size() returns a base pointer to a whole TABLE's worth
// of one component -- not a single entity -- so a system callback runs once
// per matching table, not once per entity. TableIter preserves that shape
// exactly: the engine fills one on its OWN STACK per table and calls the
// plugin's trampoline once, and the plugin's generated per-entity loop reads
// straight out of `columns[i]`, identical to what flecs's own each() compiles
// to. No heap allocation and no added engine call per entity -- see the
// plugin-abi-rework plan's zero-overhead-iteration requirement.
// ---------------------------------------------------------------------------

struct TableIter {
	void* const* columns; // one base pointer per term, term_count entries
	const uint64_t* entities; // entity ids for this table, `count` entries
	int32_t count; // entities in THIS table
	int32_t term_count;
	float delta_time;
	void* _impl; // engine-side ecs_iter_t*; plugin never dereferences this
};

using SystemFn = void (*)(TableIter*);

// Pipeline phases a system can run in. Deliberately a small, fixed set --
// the ones the engine's own default pipeline defines -- rather than exposing
// arbitrary custom phases; see rendering_world_feature.cpp for engine code
// that needs more of flecs's pipeline machinery than this.
enum class SystemPhase : uint8_t {
	PreUpdate,
	OnUpdate,
	PostUpdate,
	PreStore,
	OnStore,
};

struct SystemDesc {
	const char* name;
	SystemPhase phase;
	const ComponentId* terms; // ids this system's columns are matched in order
	int32_t term_count;
	SystemFn callback;
};

// Registers a system that runs `desc.callback` once per matching table, in
// `desc.phase`. `desc.terms` order is exactly the column order the callback
// will see in TableIter::columns -- the codegen that emits both a
// SystemDesc and its trampoline is responsible for keeping them in sync (see
// tools/codegen/extensions/ecs.py's EcsSystemModifier).
void register_system(WorldHandle world, const SystemDesc& desc);

// ---------------------------------------------------------------------------
// Entities, relationships, prefabs.
// ---------------------------------------------------------------------------

// Creates a named entity with no parent.
EntityHandle create_entity(WorldHandle world, const char* name);

// Creates a named entity as a child of `parent` (a ChildOf relationship, the
// same one WorldSim::create_entity(parent, name) already uses internally).
EntityHandle create_child_entity(WorldHandle world, EntityHandle parent, const char* name);

// Marks an already-created entity as a prefab template (EcsPrefab), so
// instantiate_prefab() can later spawn instances of it via an IsA relationship.
void mark_as_prefab(EntityHandle e);

// Creates a named entity as an instance of `prefab` (an IsA relationship).
EntityHandle instantiate_prefab(WorldHandle world, EntityHandle prefab, const char* name);

[[nodiscard]] bool entity_is_valid(EntityHandle e);
void destroy_entity(EntityHandle e);

// Parents `child` under `parent` via ChildOf, independent of creation --
// mirrors flecs's entity.child_of(parent).
void entity_child_of(EntityHandle child, EntityHandle parent);
// Returns the ChildOf target, or an invalid EntityHandle if `e` has none.
[[nodiscard]] EntityHandle entity_parent(EntityHandle e);

// Tag add/remove/has -- no component payload, e.g. WorldSim's ActiveScene marker.
void entity_add(EntityHandle e, ComponentId comp);
void entity_remove(EntityHandle e, ComponentId comp);
[[nodiscard]] bool entity_has(EntityHandle e, ComponentId comp);

// Raw byte-level get/set/emplace, matching flecs's own ecs_get_id/ecs_set_id/
// ecs_emplace_id. The typed template wrappers below are what generated and
// plugin code actually calls; these exist so ecs_api.cpp is the only place
// that needs a component's runtime size.
[[nodiscard]] const void* entity_get_raw(EntityHandle e, ComponentId comp);
[[nodiscard]] void* entity_get_mut_raw(EntityHandle e, ComponentId comp);
void entity_set_raw(EntityHandle e, ComponentId comp, const void* data, size_t size);
// is_new tells the caller whether ecs_emplace_id ran a real ctor for this
// call (see entity_emplace<T>() below, which relies on it always being true
// -- calling emplace on a component the entity already has is a caller bug,
// exactly as it is with flecs's own .emplace<T>()).
[[nodiscard]] void* entity_emplace_raw(EntityHandle e, ComponentId comp, size_t size, bool* is_new);

// ---------------------------------------------------------------------------
// Header-only hook trampolines and the ComponentDesc builder. Pure C++
// metaprogramming with no flecs dependency, so this is safe to use from
// plugin-generated code as well as engine code.
// ---------------------------------------------------------------------------

template <typename T>
struct ComponentHooks {
	static void ctor(void* ptr, int32_t count) {
		T* p = static_cast<T*>(ptr);
		for (int32_t i = 0; i < count; ++i) {
			std::construct_at(p + i);
		}
	}

	static void dtor(void* ptr, int32_t count) {
		T* p = static_cast<T*>(ptr);
		for (int32_t i = 0; i < count; ++i) {
			std::destroy_at(p + i);
		}
	}

	static void copy(void* dst, const void* src, int32_t count) {
		T* d = static_cast<T*>(dst);
		const T* s = static_cast<const T*>(src);
		for (int32_t i = 0; i < count; ++i) {
			d[i] = s[i];
		}
	}

	static void move(void* dst, void* src, int32_t count) {
		T* d = static_cast<T*>(dst);
		T* s = static_cast<T*>(src);
		for (int32_t i = 0; i < count; ++i) {
			d[i] = std::move(s[i]);
		}
	}
};

template <typename T>
[[nodiscard]] ComponentDesc make_component_desc(const char* name) {
	return ComponentDesc {
		name, sizeof(T), alignof(T),
		&ComponentHooks<T>::ctor, &ComponentHooks<T>::dtor,
		&ComponentHooks<T>::copy, &ComponentHooks<T>::move,
	};
}

// Per-(T, world) id cache. T::get_class_static() is generated into every
// reflected class's body (see generate_gen_header() in
// generate_reflection.py) as its StaticString name -- the exact same string
// ComponentModifier.emit_dir() passes to register_component() -- so this
// needs no codegen support of its own: it's a plain lookup_component() call
// under a function-local static, one indirect call the first time per (T,
// world) and a load thereafter, same cost flecs's own id cache pays.
template <typename T>
[[nodiscard]] ComponentId component_id(WorldHandle world) {
	static const ComponentId id = lookup_component(world, T::get_class_static().data());
	return id;
}

// Typed get/set/emplace, mirroring flecs::entity's .get<T>()/.set<T>()/
// .emplace<T>() -- what generated and hand-written plugin code actually
// calls. All route through the raw byte-level functions above, so no flecs
// symbol is ever referenced from the calling translation unit.

template <typename T>
[[nodiscard]] const T* entity_get(EntityHandle e, ComponentId comp) {
	return static_cast<const T*>(entity_get_raw(e, comp));
}

template <typename T>
[[nodiscard]] T* entity_get_mut(EntityHandle e, ComponentId comp) {
	return static_cast<T*>(entity_get_mut_raw(e, comp));
}

// Equivalent to flecs's ensure()+modified(): adds T if absent (via the ctor
// hook registered in register_component()) then copy-assigns `value` in.
template <typename T>
EntityHandle entity_set(EntityHandle e, ComponentId comp, const T& value) {
	entity_set_raw(e, comp, &value, sizeof(T));
	return e;
}

// Constructs T in place with `args` on storage flecs itself allocates. Caller
// must not already have `comp` on `e` -- same precondition as flecs's own
// .emplace<T>(); calling this twice for the same (entity, component) is a
// caller bug, not something this function guards against.
template <typename T, typename... Args>
EntityHandle entity_emplace(EntityHandle e, ComponentId comp, Args&&... args) {
	bool is_new = false;
	void* ptr = entity_emplace_raw(e, comp, sizeof(T), &is_new);
	std::construct_at(static_cast<T*>(ptr), std::forward<Args>(args)...);
	return e;
}

// world-taking overloads: resolve T's id via component_id<T>() instead of
// requiring the caller to have one in hand, for the common case of a plugin
// setting a handful of different-typed components on a freshly-created
// entity (see feather-example-project's _spawn_demo_scene).

template <typename T>
EntityHandle entity_set(WorldHandle world, EntityHandle e, const T& value) {
	return entity_set<T>(e, component_id<T>(world), value);
}

template <typename T, typename... Args>
EntityHandle entity_emplace(WorldHandle world, EntityHandle e, Args&&... args) {
	return entity_emplace<T>(e, component_id<T>(world), std::forward<Args>(args)...);
}

} // namespace ecs
} // namespace feather
