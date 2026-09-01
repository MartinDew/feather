#include "scripted_system.h"

#include <main/class_db.h>

#include <format>

namespace feather {

namespace {

// What the trampoline needs to turn an ecs_iter_t back into a sequence of
// per-entity calls. Owned by flecs: handed over as the system's
// callback_ctx, freed through callback_ctx_free when the system goes away.
struct ScriptedSystemContext {
	std::string name;
	ScriptedSystemCallback callback;
	// Parallel to the query's terms.
	std::vector<const ClassInfo*> infos;
	std::vector<size_t> sizes;
};

void destroy_context(void* ptr) {
	delete static_cast<ScriptedSystemContext*>(ptr);
}

// The one C callback flecs sees, for every scripted system.
//
// Per matched table it walks the rows, points each component at that row's
// storage, and calls the script once per entity. Field access stays lazy: the
// callback gets pointers and accessors, not marshalled values, so a script that
// touches one field of one component does not pay for the rest.
void run_scripted_system(ecs_iter_t* it) {
	auto* context = static_cast<ScriptedSystemContext*>(it->callback_ctx);
	if (!context) {
		return;
	}

	const size_t term_count = context->sizes.size();

	// Column bases for this table, resolved once rather than per row.
	std::vector<void*> columns(term_count, nullptr);
	for (size_t term = 0; term < term_count; term++) {
		columns[term] = ecs_field_w_size(it, context->sizes[term], static_cast<int8_t>(term));
	}

	std::vector<ScriptedSystemComponent> components(term_count);
	for (size_t term = 0; term < term_count; term++) {
		components[term].info = context->infos[term];
	}

	for (int32_t row = 0; row < it->count; row++) {
		for (size_t term = 0; term < term_count; term++) {
			void* base = columns[term];
			components[term].data =
					base ? static_cast<char*>(base) + static_cast<size_t>(row) * context->sizes[term] : nullptr;
		}

		ScriptedSystemInvocation invocation;
		invocation.entity = it->entities[row];
		invocation.components = components;
		invocation.delta_time = static_cast<double>(it->delta_time);

		context->callback(invocation);
	}
}

ecs_entity_t phase_entity(ScriptedSystemPhase phase) {
	switch (phase) {
		case ScriptedSystemPhase::OnLoad: return EcsOnLoad;
		case ScriptedSystemPhase::PostLoad: return EcsPostLoad;
		case ScriptedSystemPhase::PreUpdate: return EcsPreUpdate;
		case ScriptedSystemPhase::OnUpdate: return EcsOnUpdate;
		case ScriptedSystemPhase::OnValidate: return EcsOnValidate;
		case ScriptedSystemPhase::PostUpdate: return EcsPostUpdate;
		case ScriptedSystemPhase::PreStore: return EcsPreStore;
		case ScriptedSystemPhase::OnStore: return EcsOnStore;
	}
	return EcsOnUpdate;
}

} //namespace

Ecs::entity_t register_scripted_system(
		World& world,
		const std::string& name,
		const std::vector<std::string>& component_names,
		ScriptedSystemPhase phase,
		ScriptedSystemCallback callback,
		std::string* error
) {
	auto fail = [&error](std::string message) -> Ecs::entity_t {
		if (error) {
			*error = std::move(message);
		}
		return 0;
	};

	if (name.empty()) {
		return fail("a scripted system needs a name");
	}
	if (!callback) {
		return fail(std::format("scripted system '{}' has no callback", name));
	}
	if (component_names.empty()) {
		// A query with no terms matches every entity in the world, which is
		// never what a script meant to ask for.
		return fail(std::format("scripted system '{}' queries no components", name));
	}
	if (component_names.size() > FLECS_TERM_COUNT_MAX) {
		return fail(std::format(
				"scripted system '{}' queries {} components; flecs allows {}",
				name, component_names.size(), FLECS_TERM_COUNT_MAX
		));
	}

	auto context = std::make_unique<ScriptedSystemContext>();
	context->name = name;
	context->callback = std::move(callback);

	ecs_system_desc_t desc {};

	for (size_t term = 0; term < component_names.size(); term++) {
		const std::string& component_name = component_names[term];

		const auto component = world.lookup(component_name.c_str());
		if (!component.is_valid()) {
			return fail(std::format(
					"scripted system '{}': no component named '{}' in this world", name, component_name
			));
		}

		// The size flecs actually stores, rather than anything recomputed here:
		// for a C++ component this is the only place it is known, and for a
		// scripted one it double-checks the layout that was registered.
		const ecs_type_info_t* type_info = ecs_get_type_info(world.c_ptr(), component);
		if (!type_info || type_info->size <= 0) {
			return fail(std::format(
					"scripted system '{}': '{}' is a tag, not a component with fields", name, component_name
			));
		}

		// Field access goes through the class description, which both scripted
		// and C++ value-type components register.
		const ClassInfo* info = ClassDB::get_class_info(component_name);
		if (!info) {
			return fail(std::format(
					"scripted system '{}': '{}' has no registered class, so its fields cannot be reached",
					name, component_name
			));
		}
		if (!info->is_value_type) {
			return fail(std::format(
					"scripted system '{}': '{}' is not a value type; only component structs can be queried",
					name, component_name
			));
		}

		context->infos.push_back(info);
		context->sizes.push_back(static_cast<size_t>(type_info->size));
		desc.query.terms[term].id = component;
	}

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = name.c_str();
	const ecs_entity_t phase_id = phase_entity(phase);
	const ecs_id_t phase_pair = ecs_make_pair(EcsDependsOn, phase_id);
	const ecs_id_t add_ids[] = { phase_pair, phase_id, 0 };
	entity_desc.add = add_ids;

	desc.entity = ecs_entity_init(world.c_ptr(), &entity_desc);
	if (!desc.entity) {
		return fail(std::format("could not create an entity for scripted system '{}'", name));
	}

	desc.callback = run_scripted_system;
	desc.callback_ctx = context.get();
	desc.callback_ctx_free = destroy_context;

	const ecs_entity_t system = ecs_system_init(world.c_ptr(), &desc);
	if (!system) {
		return fail(std::format("flecs rejected scripted system '{}'", name));
	}

	// flecs owns the context now, and will free it through callback_ctx_free.
	(void)context.release();

	return system;
}

} //namespace feather
