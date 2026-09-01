#include "scripted_component.h"

#include <main/class_db.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <format>

namespace feather {

namespace {

// The storage a field of each Variant type occupies inside the component.
//
// These are the types Variant itself holds (see its InternalVariant), not
// wider ones: reading a field back has to produce exactly the Variant the
// script set, and a narrower store would silently round-trip differently.
struct FieldStorage {
	size_t size;
	size_t alignment;
};

bool storage_for(VariantType type, FieldStorage& out) {
	switch (type) {
		case VariantType::BOOL:
			out = { sizeof(bool), alignof(bool) };
			return true;
		case VariantType::INT:
			out = { sizeof(int), alignof(int) };
			return true;
		case VariantType::FLOAT:
			out = { sizeof(real_t), alignof(real_t) };
			return true;
		case VariantType::VECTOR2:
			out = { sizeof(Vector2), alignof(Vector2) };
			return true;
		case VariantType::VECTOR3:
			out = { sizeof(Vector3), alignof(Vector3) };
			return true;
		case VariantType::COLOR:
			out = { sizeof(Color), alignof(Color) };
			return true;
		default:
			// STRING/ARRAY/PATH/OBJECT/RID and the invalid ones: see the header.
			return false;
	}
}

const char* type_name(VariantType type) {
	switch (type) {
		case VariantType::NIL: return "NIL";
		case VariantType::BOOL: return "BOOL";
		case VariantType::INT: return "INT";
		case VariantType::FLOAT: return "FLOAT";
		case VariantType::VECTOR3: return "VECTOR3";
		case VariantType::VECTOR2: return "VECTOR2";
		case VariantType::VERTEX: return "VERTEX";
		case VariantType::COLOR: return "COLOR";
		case VariantType::RID: return "RID";
		case VariantType::STRING: return "STRING";
		case VariantType::ARRAY: return "ARRAY";
		case VariantType::PATH: return "PATH";
		case VariantType::OBJECT: return "OBJECT";
		default: return "INVALID";
	}
}

// Reads/writes one field of a live component instance. `base` is the raw
// component pointer -- for a value type, ClassInfo::Property's void* is the
// object itself, not a Reflected*.
template <typename T>
void fill_accessors(ScriptedFieldLayout& field) {
	const size_t offset = field.offset;
	field.getter = [offset](void* base) -> Variant {
		T value;
		// memcpy rather than a reinterpret_cast read: the field sits at a
		// computed offset in flecs-owned storage, so nothing guarantees the
		// compiler's alignment assumptions for T hold at that address.
		std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof(T));
		return Variant(value);
	};
	field.setter = [offset](void* base, Variant value) {
		auto converted = value.as<T>();
		if (!converted) {
			// Wrong type from a script is a normal mistake, not a crash.
			return;
		}
		T stored = converted.value();
		std::memcpy(static_cast<char*>(base) + offset, &stored, sizeof(T));
	};
}

bool build_accessors(ScriptedFieldLayout& field) {
	switch (field.type) {
		case VariantType::BOOL: fill_accessors<bool>(field); return true;
		case VariantType::INT: fill_accessors<int>(field); return true;
		case VariantType::FLOAT: fill_accessors<real_t>(field); return true;
		case VariantType::VECTOR2: fill_accessors<Vector2>(field); return true;
		case VariantType::VECTOR3: fill_accessors<Vector3>(field); return true;
		case VariantType::COLOR: fill_accessors<Color>(field); return true;
		default: return false;
	}
}

// Layouts outlive every caller: a component cannot be withdrawn from a world
// that may already store it, so these are only ever added to. std::deque, not
// vector, so growing the registry never invalidates a pointer handed out
// earlier.
std::deque<ScriptedComponentLayout>& layouts() {
	static std::deque<ScriptedComponentLayout> registry;
	return registry;
}

size_t align_up(size_t offset, size_t alignment) {
	return (offset + alignment - 1) & ~(alignment - 1);
}

} //namespace

Ecs::entity_t register_scripted_component(
		World& world,
		const std::string& name,
		const std::vector<ScriptedField>& fields,
		std::string* error
) {
	auto fail = [&error](std::string message) -> Ecs::entity_t {
		if (error) {
			*error = std::move(message);
		}
		return 0;
	};

	if (name.empty()) {
		return fail("a scripted component needs a name");
	}
	if (fields.empty()) {
		// A zero-size flecs component is a tag, which behaves differently
		// enough (no storage, so nothing to get or set) that silently
		// producing one would be a surprise.
		return fail(std::format("scripted component '{}' has no fields", name));
	}
	if (world.lookup(name.c_str()).is_valid()) {
		return fail(std::format("'{}' already exists in the world", name));
	}

	// Lay the fields out the way a C compiler would: each at the next offset
	// meeting its alignment, the whole rounded up to the widest field's
	// alignment so an array of them stays aligned.
	ScriptedComponentLayout layout;
	layout.name = name;
	layout.fields.reserve(fields.size());

	size_t offset = 0;
	size_t max_alignment = 1;
	for (const ScriptedField& field : fields) {
		if (field.name.empty()) {
			return fail(std::format("scripted component '{}' has an unnamed field", name));
		}

		FieldStorage storage {};
		if (!storage_for(field.type, storage)) {
			return fail(std::format(
					"scripted component '{}': field '{}' has type {}, which has no fixed layout",
					name, field.name, type_name(field.type)
			));
		}

		offset = align_up(offset, storage.alignment);
		max_alignment = std::max(max_alignment, storage.alignment);

		ScriptedFieldLayout resolved;
		resolved.name = field.name;
		resolved.type = field.type;
		resolved.offset = offset;
		if (!build_accessors(resolved)) {
			return fail(std::format("scripted component '{}': field '{}' has no accessor", name, field.name));
		}
		layout.fields.push_back(std::move(resolved));

		offset += storage.size;
	}
	const size_t total_size = align_up(offset, max_alignment);
	layout.size = total_size;
	layout.alignment = max_alignment;

	// The same accessors serve reflection and the system trampoline: one
	// closure per field, shared, so the two can never read a field differently.
	std::vector<ClassInfo::Property> properties;
	properties.reserve(layout.fields.size());
	for (const ScriptedFieldLayout& field : layout.fields) {
		properties.push_back(ClassInfo::Property {
				.name = StaticString(field.name),
				.type = field.type,
				.getter = field.getter,
				.setter = field.setter,
		});
	}

	// ClassDB first: it is the half that can refuse (a name collision with a
	// C++ class), and a refused registration must not leave a component behind
	// in the world that nothing can read.
	if (!ClassDB::register_scripted_value_class(StaticString(name), std::move(properties))) {
		return fail(std::format("'{}' is already a registered class", name));
	}

	ecs_entity_desc_t entity_desc {};
	entity_desc.name = name.c_str();
	const ecs_entity_t entity = ecs_entity_init(world.c_ptr(), &entity_desc);
	if (!entity) {
		return fail(std::format("could not create an entity for scripted component '{}'", name));
	}

	ecs_component_desc_t component_desc {};
	component_desc.entity = entity;
	component_desc.type.size = static_cast<ecs_size_t>(total_size);
	component_desc.type.alignment = static_cast<ecs_size_t>(max_alignment);
	component_desc.type.name = name.c_str();
	// Zero-initialized on add, so a script reads defined values before it has
	// written anything. Every supported field type is trivially copyable, so
	// this is the only hook the storage needs.
	component_desc.type.hooks.ctor = flecs_default_ctor;

	const ecs_entity_t component = ecs_component_init(world.c_ptr(), &component_desc);
	if (!component) {
		return fail(std::format("flecs rejected the layout for scripted component '{}'", name));
	}

	layout.component = component;
	layouts().push_back(std::move(layout));

	return component;
}

const ScriptedComponentLayout* find_scripted_component(Ecs::entity_t component) {
	for (const ScriptedComponentLayout& layout : layouts()) {
		if (layout.component == component) {
			return &layout;
		}
	}
	return nullptr;
}

const ScriptedComponentLayout* find_scripted_component(const std::string& name) {
	for (const ScriptedComponentLayout& layout : layouts()) {
		if (layout.name == name) {
			return &layout;
		}
	}
	return nullptr;
}

} //namespace feather
