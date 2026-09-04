// world.cpp
#include "world.h"

#include <framework/assert.h>
#include <main/class_db.h>

namespace feather {

World::World() : _ecs_world(std::make_unique<flecs::world>()) {
}

World::~World() = default;

World::World(World&&) noexcept = default;
World& World::operator=(World&&) noexcept = default;

bool World::progress(float delta) {
	return _ecs_world->progress(delta);
}

ecs_entity_t World::entity_create() {
	return _ecs_world->entity().id();
}

void World::entity_destroy(ecs_entity_t entity) {
	_ecs_world->entity(entity).destruct();
}

bool World::entity_is_valid(ecs_entity_t entity) const {
	return _ecs_world->entity(entity).is_valid();
}

ecs_entity_t World::register_component_type(StaticString class_name) {
	auto it = _component_type_map.find(class_name);
	if (it != _component_type_map.end()) {
		return it->second;
	}

	const ClassInfo* info = ClassDB::get_class_info(class_name);
	fassert(info != nullptr, "Cannot register unreflected type as component in World");

	ecs_component_desc_t desc = {};
	desc.entity.name = info->name.data();

	// Calculate size from property types if available, or default to standard size/align
	size_t component_size = 0;
	size_t component_align = alignof(std::max_align_t);

	for (const auto& prop : info->properties) {
		switch (prop.type) {
		case VariantType::BOOL:
			component_size += sizeof(bool);
			break;
		case VariantType::INT:
			component_size += sizeof(int);
			break;
		case VariantType::FLOAT:
			component_size += sizeof(real_t);
			break;
		case VariantType::VECTOR2:
			component_size += sizeof(Vector2);
			break;
		case VariantType::VECTOR3:
			component_size += sizeof(Vector3);
			break;
		case VariantType::COLOR:
			component_size += sizeof(Color);
			break;
		default:
			component_size += sizeof(void*);
			break;
		}
	}

	desc.size = component_size > 0 ? component_size : sizeof(Variant);
	desc.alignment = component_align;

	ecs_entity_t comp_id = ecs_component_init(_ecs_world->c_ptr(), &desc);
	_component_type_map[class_name] = comp_id;

	return comp_id;
}

void World::set_component_value(ecs_entity_t entity, StaticString class_name, const Variant& value) {
	ecs_entity_t comp_id = register_component_type(class_name);
	const ClassInfo* info = ClassDB::get_class_info(class_name);

	if (info && info->is_value_type) {
		// Set opaque void pointer data for reflected FSTRUCT value types
		void* comp_ptr = ecs_get_mut_id(_ecs_world->c_ptr(), entity, comp_id);
		if (!comp_ptr) {
			ecs_set_id(_ecs_world->c_ptr(), entity, comp_id, info->properties.size() * sizeof(Variant), nullptr);
			comp_ptr = ecs_get_mut_id(_ecs_world->c_ptr(), entity, comp_id);
		}

		for (const auto& prop : info->properties) {
			if (prop.setter) {
				Variant field_val = value.get_internal(prop.name);
				prop.setter(comp_ptr, field_val);
			}
		}
		ecs_modified_id(_ecs_world->c_ptr(), entity, comp_id);
	}
	else {
		_ecs_world->entity(entity).set<Variant>(value);
	}
}

Variant World::get_component_value(ecs_entity_t entity, StaticString class_name) const {
	auto it = _component_type_map.find(class_name);
	if (it == _component_type_map.end()) {
		return {};
	}

	ecs_entity_t comp_id = it->second;
	const void* comp_ptr = ecs_get_id(_ecs_world->c_ptr(), entity, comp_id);
	if (!comp_ptr) {
		return {};
	}

	const ClassInfo* info = ClassDB::get_class_info(class_name);
	if (info && info->object_create_func) {
		Variant obj = info->object_create_func();
		for (const auto& prop : info->properties) {
			if (prop.getter) {
				Variant field_val = prop.getter(const_cast<void*>(comp_ptr));
				obj.set_internal(prop.name, field_val);
			}
		}
		return obj;
	}

	return {};
}

bool World::has_component(ecs_entity_t entity, StaticString class_name) const {
	auto it = _component_type_map.find(class_name);
	if (it == _component_type_map.end()) {
		return false;
	}
	return ecs_has_id(_ecs_world->c_ptr(), entity, it->second);
}

void World::remove_component(ecs_entity_t entity, StaticString class_name) {
	auto it = _component_type_map.find(class_name);
	if (it != _component_type_map.end()) {
		ecs_remove_id(_ecs_world->c_ptr(), entity, it->second);
	}
}

ecs_entity_t
World::create_system(std::string_view system_name, std::span<const StaticString> components, StaticString method_name) {
	ecs_system_desc_t sys_desc = {};
	sys_desc.entity.name = system_name.data();

	for (size_t i = 0; i < components.size() && i < FLECS_TERM_COUNT_MAX; ++i) {
		ecs_entity_t comp_id = register_component_type(components[i]);
		sys_desc.query.filter.terms[i].id = comp_id;
	}

	sys_desc.callback = [](ecs_iter_t* it) {
		StaticString method = *static_cast<StaticString*>(it->ctx);

		for (int i = 0; i < it->count; ++i) {
			ecs_entity_t e = it->entities[i];
			Variant entity_var(static_cast<int>(e));

			// Look up method callable stored in ClassDB or dispatch through script runner
			const ClassInfo* global_info = ClassDB::get_class_info("Simulation"_ss);
			if (global_info) {
				for (const auto& m : global_info->methods) {
					if (m.name == method) {
						Callable callable = m.callable;
						Variant args[] = { entity_var, Variant(it->delta_time) };
						callable.call(args);
					}
				}
			}
		}
	};

	StaticString* ctx_method = new StaticString(method_name);
	sys_desc.ctx = ctx_method;

	return ecs_system_init(_ecs_world->c_ptr(), &sys_desc);
}

flecs::world& World::get_flecs_world() {
	return *_ecs_world;
}

const flecs::world& World::get_flecs_world() const {
	return *_ecs_world;
}

} // namespace feather