#!/usr/bin/env python3
"""
generate_bindings.py -- turns the engine's `--dump-api` JSON into a
self-contained C++ header of typed bindings over core/extension/feather_interface.h.

Deliberately a separate script from generate_reflection.py, not an extension
of it: different input (JSON, not C++ headers), different output (one
generated tree, not sidecar .gen.h next to sources), different trigger (the
engine binary changed, not project sources), different cadence (an engine
upgrade, not every build). None of generate_reflection.py's load-bearing
contracts (the __LINE__-keyed macro naming, #if-hoisting, per-directory
modifier scoping) apply here -- there's no C++ parsing at all.

Every bound method's parameter/return types are one of the FeatherVariantType
values. Method calls pick one of two ABI entry points depending on whether
every type involved has a fixed C layout (see feather_interface.h's comment
on method_ptrcall):
  - POD types (NIL/BOOL/INT/FLOAT/RID/OBJECT/VECTOR2/VECTOR3/COLOR/VERTEX):
    method_ptrcall, no Variant built on either side of the call.
  - STRING/PATH: method_variant_call, via variant_new/variant_set_string_utf8/
    variant_get_string_utf8/variant_destroy.
  - ARRAY: skipped entirely for now -- VariantArray has no C ABI story yet
    (see feather_interface.h); a method with an ARRAY in its signature is
    left unbound rather than half-implemented.
"""

import argparse
import json
import sys
from pathlib import Path

POD_TYPES = {
    "NIL", "BOOL", "INT", "FLOAT", "RID", "OBJECT",
    "VECTOR2", "VECTOR3", "COLOR", "VERTEX",
}
STRING_TYPES = {"STRING", "PATH"}
UNSUPPORTED_TYPES = {"ARRAY", "INVALID"}

# FeatherVariantType -> (public C++ param type, storage/ptrcall-slot type)
PARAM_CPP_TYPE = {
    "BOOL": "bool",
    "INT": "int32_t",
    "FLOAT": "float",
    "RID": "uint64_t",
    "OBJECT": "FeatherObjectPtr",
    "VECTOR2": "FeatherVector2",
    "VECTOR3": "FeatherVector3",
    "COLOR": "FeatherColor",
    "VERTEX": "FeatherVertex",
}
STORAGE_TYPE = {
    "BOOL": "bool",
    "INT": "int32_t",
    "FLOAT": "float",
    "RID": "uint64_t",
    "OBJECT": "FeatherObjectPtr",
    "VECTOR2": "FeatherVector2",
    "VECTOR3": "FeatherVector3",
    "COLOR": "FeatherColor",
    "VERTEX": "FeatherVertex",
}


def write_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.write_text(content, encoding="utf-8")
    return True


def method_is_ptrcall_eligible(method: dict) -> bool:
    types = [method["return_type"]] + method["param_types"]
    return all(t in POD_TYPES for t in types)


def method_is_bindable(method: dict) -> bool:
    types = [method["return_type"]] + method["param_types"]
    return not any(t in UNSUPPORTED_TYPES for t in types)


def emit_ptrcall_method(cls_name: str, method: dict) -> list:
    ret_type = method["return_type"]
    params = method["param_types"]
    cpp_params = ", ".join(f"{PARAM_CPP_TYPE[t]} arg{i}" for i, t in enumerate(params))
    cpp_ret = "void" if ret_type == "NIL" else PARAM_CPP_TYPE[ret_type]

    lines = [f"\t{cpp_ret} {method['name']}({cpp_params}) {{"]
    lines.append(f'\t\tstatic FeatherMethodPtr method = _bindings::get_method("{cls_name}", "{method["name"]}");')
    for i, t in enumerate(params):
        storage = STORAGE_TYPE[t]
        if storage == PARAM_CPP_TYPE[t]:
            lines.append(f"\t\t{storage} arg{i}_storage = arg{i};")
        else:
            lines.append(f"\t\t{storage} arg{i}_storage = static_cast<{storage}>(arg{i});")
    if params:
        arg_ptrs = ", ".join(f"&arg{i}_storage" for i in range(len(params)))
        lines.append(f"\t\tconst void* args[] = {{ {arg_ptrs} }};")
        args_expr = "args"
    else:
        args_expr = "nullptr"
    if ret_type == "NIL":
        lines.append(f"\t\t_bindings::ptrcall(method, _handle, {args_expr}, nullptr);")
    else:
        ret_storage = STORAGE_TYPE[ret_type]
        lines.append(f"\t\t{ret_storage} ret_storage {{}};")
        lines.append(f"\t\t_bindings::ptrcall(method, _handle, {args_expr}, &ret_storage);")
        if ret_storage == cpp_ret:
            lines.append("\t\treturn ret_storage;")
        else:
            lines.append(f"\t\treturn static_cast<{cpp_ret}>(ret_storage);")
    lines.append("\t}")
    return lines


def emit_variant_method(cls_name: str, method: dict) -> list:
    ret_type = method["return_type"]
    params = method["param_types"]

    def cpp_param_type(t):
        return "const std::string&" if t in STRING_TYPES else PARAM_CPP_TYPE[t]

    cpp_params = ", ".join(f"{cpp_param_type(t)} arg{i}" for i, t in enumerate(params))
    cpp_ret = "void" if ret_type == "NIL" else ("std::string" if ret_type in STRING_TYPES else PARAM_CPP_TYPE[ret_type])

    lines = [f"\t{cpp_ret} {method['name']}({cpp_params}) {{"]
    lines.append(f'\t\tstatic FeatherMethodPtr method = _bindings::get_method("{cls_name}", "{method["name"]}");')
    for i, t in enumerate(params):
        lines.append(f"\t\tFeatherVariantPtr arg{i}_v = _bindings::variant_new();")
        if t in STRING_TYPES:
            lines.append(f"\t\t_bindings::variant_set_string_utf8(arg{i}_v, arg{i}.data(), arg{i}.size());")
        else:
            lines.append(f"\t\t{STORAGE_TYPE[t]} arg{i}_storage = arg{i};")
            lines.append(
                f"\t\t_bindings::variant_from_ptr(arg{i}_v, FEATHER_VARIANT_{t}, &arg{i}_storage);")
    if ret_type != "NIL":
        lines.append("\t\tFeatherVariantPtr ret_v = _bindings::variant_new();")
    ret_arg = "ret_v" if ret_type != "NIL" else "nullptr"
    if params:
        args_list = ", ".join(f"arg{i}_v" for i in range(len(params)))
        lines.append(f"\t\tFeatherVariantPtr args[] = {{ {args_list} }};")
        argv = "args"
    else:
        argv = "nullptr"
    lines.append(f"\t\t_bindings::variant_call(method, _handle, {argv}, {len(params)}, {ret_arg});")
    for i in range(len(params)):
        lines.append(f"\t\t_bindings::variant_destroy(arg{i}_v);")
    if ret_type == "NIL":
        pass
    elif ret_type in STRING_TYPES:
        lines.append("\t\tstd::string result(_bindings::variant_get_string_utf8(ret_v, nullptr, 0), '\\0');")
        lines.append("\t\t_bindings::variant_get_string_utf8(ret_v, result.data(), result.size() + 1);")
        lines.append("\t\t_bindings::variant_destroy(ret_v);")
        lines.append("\t\treturn result;")
    else:
        storage = STORAGE_TYPE[ret_type]
        lines.append(f"\t\t{storage} ret_storage {{}};")
        lines.append(f"\t\t_bindings::variant_to_ptr(ret_v, FEATHER_VARIANT_{ret_type}, &ret_storage);")
        lines.append("\t\t_bindings::variant_destroy(ret_v);")
        cpp_ret2 = PARAM_CPP_TYPE[ret_type]
        lines.append("\t\treturn ret_storage;" if storage == cpp_ret2 else f"\t\treturn static_cast<{cpp_ret2}>(ret_storage);")
    lines.append("\t}")
    return lines


def emit_class(cls: dict) -> list:
    name = cls["name"]
    lines = [f"class {name} {{", "protected:", "\tFeatherObjectPtr _handle;", "", "public:",
             f"\texplicit {name}(FeatherObjectPtr handle) : _handle(handle) {{}}",
             "\tFeatherObjectPtr handle() const { return _handle; }", ""]
    for method in cls["methods"]:
        if not method_is_bindable(method):
            lines.append(f"\t// {method['name']}: skipped, involves a VariantType with no C ABI layout yet")
            continue
        if method_is_ptrcall_eligible(method):
            lines.extend(emit_ptrcall_method(name, method))
        else:
            lines.extend(emit_variant_method(name, method))
    lines.append("};")
    return lines


HEADER_PREAMBLE = """\
// AUTO-GENERATED by tools/codegen/generate_bindings.py from a --dump-api
// JSON dump. Do not edit by hand -- re-run the generator instead.
#pragma once

#include <extension/feather_interface.h>

#include <cstdint>
#include <string>

namespace feather {

// Resolved once via feather_extension_init's FeatherGetProcAddress; every
// generated method call goes through these. init() must run before any
// generated class method is called.
namespace _bindings {

inline FeatherInterfaceLog log_fn = nullptr;
inline FeatherInterfaceClassdbGetClass classdb_get_class = nullptr;
inline FeatherInterfaceClassdbClassGetMethod classdb_class_get_method = nullptr;
inline FeatherInterfaceClassdbRegisterExtensionClass classdb_register_extension_class_fn = nullptr;
inline FeatherInterfaceObjectCreate object_create_fn = nullptr;
inline FeatherInterfaceObjectDestroy object_destroy_fn = nullptr;
inline FeatherInterfaceMethodPtrcall method_ptrcall_fn = nullptr;
inline FeatherInterfaceMethodVariantCall method_variant_call_fn = nullptr;
inline FeatherInterfaceVariantNew variant_new_fn = nullptr;
inline FeatherInterfaceVariantDestroy variant_destroy_fn = nullptr;
inline FeatherInterfaceVariantFromPtr variant_from_ptr_fn = nullptr;
inline FeatherInterfaceVariantToPtr variant_to_ptr_fn = nullptr;
inline FeatherInterfaceVariantGetStringUtf8 variant_get_string_utf8_fn = nullptr;
inline FeatherInterfaceVariantSetStringUtf8 variant_set_string_utf8_fn = nullptr;
inline FeatherInterfaceEcsGetWorld ecs_get_world_fn = nullptr;
inline FeatherInterfaceEcsRegisterComponent ecs_register_component_fn = nullptr;
inline FeatherInterfaceEcsRegisterSystem ecs_register_system_fn = nullptr;
inline FeatherInterfaceEcsEntityCreate ecs_entity_create_fn = nullptr;
inline FeatherInterfaceEcsEntitySet ecs_entity_set_fn = nullptr;
inline FeatherInterfaceEcsEntityGetMut ecs_entity_get_mut_fn = nullptr;

// The only function a plugin ever has to resolve by hand: every generated
// binding and every feather::ecs helper goes through the cached pointers
// above from here on, so this runs once, in feather_extension_init, and
// nowhere else needs a FeatherGetProcAddress parameter.
inline void init(FeatherGetProcAddress get_proc_address) {
	log_fn = (FeatherInterfaceLog)get_proc_address("feather_log");
	classdb_get_class = (FeatherInterfaceClassdbGetClass)get_proc_address("classdb_get_class");
	classdb_class_get_method = (FeatherInterfaceClassdbClassGetMethod)get_proc_address("classdb_class_get_method");
	classdb_register_extension_class_fn =
			(FeatherInterfaceClassdbRegisterExtensionClass)get_proc_address("classdb_register_extension_class");
	object_create_fn = (FeatherInterfaceObjectCreate)get_proc_address("object_create");
	object_destroy_fn = (FeatherInterfaceObjectDestroy)get_proc_address("object_destroy");
	method_ptrcall_fn = (FeatherInterfaceMethodPtrcall)get_proc_address("method_ptrcall");
	method_variant_call_fn = (FeatherInterfaceMethodVariantCall)get_proc_address("method_variant_call");
	variant_new_fn = (FeatherInterfaceVariantNew)get_proc_address("variant_new");
	variant_destroy_fn = (FeatherInterfaceVariantDestroy)get_proc_address("variant_destroy");
	variant_from_ptr_fn = (FeatherInterfaceVariantFromPtr)get_proc_address("variant_from_ptr");
	variant_to_ptr_fn = (FeatherInterfaceVariantToPtr)get_proc_address("variant_to_ptr");
	variant_get_string_utf8_fn = (FeatherInterfaceVariantGetStringUtf8)get_proc_address("variant_get_string_utf8");
	variant_set_string_utf8_fn = (FeatherInterfaceVariantSetStringUtf8)get_proc_address("variant_set_string_utf8");
	ecs_get_world_fn = (FeatherInterfaceEcsGetWorld)get_proc_address("ecs_get_world");
	ecs_register_component_fn = (FeatherInterfaceEcsRegisterComponent)get_proc_address("ecs_register_component");
	ecs_register_system_fn = (FeatherInterfaceEcsRegisterSystem)get_proc_address("ecs_register_system");
	ecs_entity_create_fn = (FeatherInterfaceEcsEntityCreate)get_proc_address("ecs_entity_create");
	ecs_entity_set_fn = (FeatherInterfaceEcsEntitySet)get_proc_address("ecs_entity_set");
	ecs_entity_get_mut_fn = (FeatherInterfaceEcsEntityGetMut)get_proc_address("ecs_entity_get_mut");
}

inline FeatherMethodPtr get_method(const char* cls_name, const char* method_name) {
	FeatherClassPtr cls = classdb_get_class(cls_name);
	return cls ? classdb_class_get_method(cls, method_name) : nullptr;
}
inline FeatherObjectPtr create(const char* cls_name) { return object_create_fn(cls_name); }
inline void destroy(FeatherObjectPtr obj) { object_destroy_fn(obj); }
inline bool register_extension_class(
		const char* class_name, const char* parent_class_name, const FeatherExtensionClassInfo* info) {
	return classdb_register_extension_class_fn(nullptr, class_name, parent_class_name, info);
}
inline void log(const char* message) {
	if (log_fn)
		log_fn(message);
}
inline void ptrcall(FeatherMethodPtr m, FeatherObjectPtr o, const void* const* a, void* r) { method_ptrcall_fn(m, o, a, r); }
inline void variant_call(FeatherMethodPtr m, FeatherObjectPtr o, const FeatherVariantPtr* a, size_t n, FeatherVariantPtr r) {
	method_variant_call_fn(m, o, a, n, r);
}
inline FeatherVariantPtr variant_new() { return variant_new_fn(); }
inline void variant_destroy(FeatherVariantPtr v) { variant_destroy_fn(v); }
inline void variant_from_ptr(FeatherVariantPtr v, FeatherVariantType t, const void* s) { variant_from_ptr_fn(v, t, s); }
inline bool variant_to_ptr(FeatherVariantPtr v, FeatherVariantType t, void* d) { return variant_to_ptr_fn(v, t, d); }
inline size_t variant_get_string_utf8(FeatherVariantPtr v, char* d, size_t cap) { return variant_get_string_utf8_fn(v, d, cap); }
inline void variant_set_string_utf8(FeatherVariantPtr v, const char* s, size_t len) { variant_set_string_utf8_fn(v, s, len); }

} // namespace _bindings

// Typed sugar over the six raw ecs_* functions -- registers/creates/iterates
// by C++ type instead of by hand-built FeatherTableIter/column-index code.
// Still just those six functions underneath: no new ABI surface, and engine
// code never sees this layer (it's generated into the plugin only).
namespace ecs {

namespace detail {

template <class T>
inline FeatherComponentId g_component_id = 0;

template <class T>
struct EachContext {
	void (*callback)(T&, float delta_time);
};

template <class T>
void each_trampoline(FeatherTableIter* it) {
	auto* ctx = static_cast<EachContext<T>*>(it->user_data);
	auto* items = static_cast<T*>(it->columns[0]);
	for (int32_t i = 0; i < it->count; ++i) {
		ctx->callback(items[i], it->delta_time);
	}
}

} // namespace detail

// Must be called once per component type, before spawn<T>/get_mut<T>/each<T>.
template <class T>
void register_component(const char* name) {
	detail::g_component_id<T> = _bindings::ecs_register_component_fn(
			_bindings::ecs_get_world_fn(), name, static_cast<uint32_t>(sizeof(T)), static_cast<uint32_t>(alignof(T)));
}

template <class T>
FeatherComponentId component_id() {
	return detail::g_component_id<T>;
}

// Registers a system running `callback` once per entity carrying T, every
// tick. Each call gets its own context (via FeatherTableIter::user_data), so
// multiple systems over the same T don't collide.
template <class T>
void each(const char* system_name, FeatherSystemPhase phase, void (*callback)(T&, float delta_time)) {
	// Leaked deliberately, matching the rest of the ABI's no-unregister story.
	auto* ctx = new detail::EachContext<T> { callback };
	FeatherComponentId terms[] = { component_id<T>() };
	_bindings::ecs_register_system_fn(
			_bindings::ecs_get_world_fn(), system_name, phase, terms, 1, &detail::each_trampoline<T>, ctx);
}

template <class T>
FeatherEntityId spawn(const char* entity_name, const T& initial) {
	FeatherWorldPtr world = _bindings::ecs_get_world_fn();
	FeatherEntityId entity = _bindings::ecs_entity_create_fn(world, entity_name);
	_bindings::ecs_entity_set_fn(world, entity, component_id<T>(), &initial, sizeof(T));
	return entity;
}

template <class T>
T* get_mut(FeatherEntityId entity) {
	return static_cast<T*>(_bindings::ecs_entity_get_mut_fn(_bindings::ecs_get_world_fn(), entity, component_id<T>()));
}

} // namespace ecs

"""

HEADER_POSTAMBLE = """
} // namespace feather
"""


def generate(api: dict) -> str:
    parts = [HEADER_PREAMBLE]
    for cls in api["classes"]:
        parts.append("\n".join(emit_class(cls)))
        parts.append("")
    parts.append(HEADER_POSTAMBLE)
    return "\n".join(parts)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", type=Path, required=True, help="path to a --dump-api JSON file")
    ap.add_argument("--out", type=Path, required=True, help="output directory")
    args = ap.parse_args()

    api = json.loads(args.api.read_text(encoding="utf-8"))
    content = generate(api)

    args.out.mkdir(parents=True, exist_ok=True)
    out_path = args.out / "feather_bindings.gen.h"
    changed = write_if_changed(out_path, content)
    print(f"{'wrote' if changed else 'unchanged'}: {out_path}")


if __name__ == "__main__":
    sys.exit(main())
