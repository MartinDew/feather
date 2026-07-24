#!/usr/bin/env python3
"""
generate_reflection.py — FeatherEngine reflection code generator.

Replaces the old regex-based generate_core_registers.py. For every reflected
class (one that uses the FCLASS(...) macro) this tool:

  * recovers the class name and parent from the C++ declaration,
  * extracts member variables (with C++ accessibility) and their [[attributes]],
  * extracts bindable methods (opt-out by default, opt-in per class),
  * emits a per-header "<name>.gen.h" holding the Unreal-style GEN_BODY macro
    (reflection boilerplate + generated getters/setters + optional singleton
    boilerplate + the _bind_members() declaration), and
  * emits, per top-level subfolder, a "register_<sub>_types.gen.{h,cpp}" that
    defines every _bind_members() body and the register_<sub>_types() entry
    point the engine calls manually.

Parsing is done by invoking the `clang` compiler with `-ast-dump=json` (from the
xrepo `llvm` package) and reading the JSON with the Python standard library — no
libclang bindings, no pip, no vendored files. Headers are parsed with
-DFEATHER_REFLECTION_PARSER=1 so FCLASS expands to nothing and parsing never
depends on generated output.

All writes go through write_if_changed(), so unchanged files keep their mtime and
xmake performs no needless rebuilds.
"""

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field as dc_field
from pathlib import Path

GENERATED_NOTICE = (
    "// THIS FILE IS AUTO-GENERATED — DO NOT EDIT BY HAND.\n"
    "// Re-run tools/generate_reflection.py to refresh.\n"
)

ACCESS_KEYWORDS = {"public", "protected", "private"}
ACCESS_ENUM = {"public": "AccessLevel::Public", "protected": "AccessLevel::Protected", "private": "AccessLevel::Private"}

# FCLASS(...) invocation, capturing the modifier list. Matches FCLASS( ... ) but
# not FCLASS_ANYTHING( — the trailing lookahead keeps us from matching a longer
# identifier (there are no other FCLASS_* macros anymore, but be safe).
_FCLASS_RE = re.compile(r"\bFCLASS\s*\(([^)]*)\)")
_ATTR_RE = re.compile(r"\[\[(.*?)\]\]", re.DOTALL)


# --------------------------------------------------------------------------- #
# Data model
# --------------------------------------------------------------------------- #

@dataclass
class PropertyPlan:
    prop_name: str          # reflected name (leading underscore stripped)
    member_name: str        # C++ member, e.g. "_foo"
    type_spelling: str      # C++ type, e.g. "float"
    # Getter: (kind, value). kind in {"none","generate","manual"}.
    getter_kind: str = "none"
    getter_access: str = "public"      # accessibility of the getter
    getter_method: str = ""            # method name (generated or manual)
    setter_kind: str = "none"
    setter_access: str = "public"
    setter_method: str = ""


@dataclass
class MethodPlan:
    name: str
    access: str             # public / protected / private
    strict: bool            # true when force-bound via [[method]] (hard error if unbindable)


@dataclass
class ClassDesc:
    name: str
    parent: str
    header: Path
    fclass_line: int
    modifiers: set = dc_field(default_factory=set)
    is_singleton: bool = False
    is_abstract: bool = False
    explicit_methods: bool = False
    properties: list = dc_field(default_factory=list)     # list[PropertyPlan]
    gen_getters: list = dc_field(default_factory=list)    # (access, code) inline accessor defs
    methods: list = dc_field(default_factory=list)        # list[MethodPlan]


# --------------------------------------------------------------------------- #
# Attribute parsing
# --------------------------------------------------------------------------- #

def _split_top_level(s: str) -> list:
    """Split on commas that are not inside parentheses."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            depth += 1
            cur += ch
        elif ch == ")":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def parse_field_attributes(prefix_text: str) -> dict:
    """
    Parse the [[...]] attributes that appear immediately before a member.
    Returns a dict of tokens, e.g. {"get": None, "set": "private"} or
    {"ignore": None}. Each token may carry a single parenthesised argument.
    """
    tokens: dict = {}
    for group in _ATTR_RE.findall(prefix_text):
        for tok in _split_top_level(group):
            m = re.match(r"^(\w+)\s*(?:\(\s*(\w+)\s*\))?$", tok)
            if not m:
                continue
            tokens[m.group(1)] = m.group(2)  # arg or None
    return tokens


# --------------------------------------------------------------------------- #
# clang JSON AST parsing
# --------------------------------------------------------------------------- #

def run_clang_ast(clang: str, header: Path, includes: list, extra_args: list) -> dict:
    args = [
        clang, "-x", "c++", "-std=c++23", "-fsyntax-only",
        "-Xclang", "-ast-dump=json",
        "-DFEATHER_REFLECTION_PARSER=1",
        "-Wno-attributes", "-Wno-unknown-attributes",
        "-ferror-limit=0",
    ]
    for inc in includes:
        args += ["-I", str(inc)]
    args += extra_args
    args.append(str(header))
    proc = subprocess.run(args, capture_output=True, text=True)
    # clang emits the JSON on stdout even with parse errors; only bail if empty.
    if not proc.stdout.strip():
        raise RuntimeError(f"clang produced no AST for {header}:\n{proc.stderr}")
    return json.loads(proc.stdout)


def _node_file(node: dict, current: str) -> str:
    """clang JSON only records 'file' when it changes; inherit otherwise."""
    loc = node.get("loc") or {}
    if "file" in loc:
        return loc["file"]
    # range.begin may also carry the file marker
    rng = node.get("range") or {}
    beg = rng.get("begin") or {}
    if "file" in beg:
        return beg["file"]
    return current


def collect_records(node: dict, target: str, current_file: str, found: list):
    """Walk the AST in document order, tracking the current file, collecting the
    top-level CXXRecordDecl definitions that live in the target header."""
    current_file = _node_file(node, current_file)
    for child in node.get("inner", []) or []:
        cf = _node_file(child, current_file)
        if child.get("kind") == "CXXRecordDecl" and child.get("name") and child.get("inner") \
                and child.get("tagUsed") in ("class", "struct") and cf == target:
            found.append(child)
            # do not recurse into members looking for more top-level records
        else:
            collect_records(child, target, cf, found)
        current_file = cf


def _base_name(record: dict) -> str:
    for base in record.get("bases", []) or []:
        qt = (base.get("type") or {}).get("qualType", "")
        # strip namespaces and template args -> simple name
        simple = qt.split("<")[0].strip().split("::")[-1]
        if simple:
            return simple
    return ""


def _member_line(node: dict) -> int:
    loc = node.get("loc") or {}
    if "line" in loc:
        return loc["line"]
    rng = node.get("range") or {}
    beg = rng.get("begin") or {}
    return beg.get("line", 0)


def _member_offset(node: dict) -> int:
    loc = node.get("loc") or {}
    if "offset" in loc:
        return loc["offset"]
    rng = node.get("range") or {}
    beg = rng.get("begin") or {}
    return beg.get("offset", -1)


def _qual_return_and_params(qual_type: str):
    """From a method qualType like 'float (int, bool) const' return
    (is_const, param_count)."""
    is_const = bool(re.search(r"\)\s*const\b", qual_type))
    m = re.search(r"\(([^)]*)\)", qual_type)
    params = []
    if m and m.group(1).strip() and m.group(1).strip() != "void":
        params = _split_top_level(m.group(1))
    return is_const, len(params)


# --------------------------------------------------------------------------- #
# Building the class descriptor from a record + raw source
# --------------------------------------------------------------------------- #

def build_class(record: dict, header: Path, text: str, fclass_index: list) -> ClassDesc:
    name = record["name"]
    parent = _base_name(record)

    # Find the FCLASS occurrence that belongs to this record (first one at/after
    # the record's opening, before the next record). Matched by source order.
    rng = record.get("range") or {}
    start = (rng.get("begin") or {}).get("offset", 0)
    end = (rng.get("end") or {}).get("offset", len(text))
    modifiers, fclass_line = set(), _member_line(record)
    for (off, line, args) in fclass_index:
        if start <= off <= end:
            modifiers = {a.strip() for a in args.split(",") if a.strip()}
            fclass_line = line
            break

    cls = ClassDesc(
        name=name, parent=parent, header=header, fclass_line=fclass_line,
        modifiers=modifiers,
        is_singleton="singleton" in modifiers,
        is_abstract="abstract" in modifiers,
        explicit_methods="explicit_methods" in modifiers,
    )

    # Walk members in order, tracking access. struct defaults to public, class to
    # private. clang usually annotates each member with "access" too; prefer it.
    default_access = "public" if record.get("tagUsed") == "struct" else "private"
    current_access = default_access

    method_names: dict = {}
    for m in record.get("inner", []) or []:
        if m.get("kind") == "AccessSpecDecl":
            current_access = m.get("access", current_access)
        elif m.get("kind") == "CXXMethodDecl":
            method_names[m.get("name", "")] = method_names.get(m.get("name", ""), 0) + 1

    for m in record.get("inner", []) or []:
        kind = m.get("kind")
        access = m.get("access", current_access)
        if kind == "AccessSpecDecl":
            current_access = m.get("access", current_access)
            continue
        access = m.get("access", current_access)

        if kind == "FieldDecl":
            _handle_field(cls, m, text, access)
        elif kind == "CXXMethodDecl":
            _handle_method(cls, m, text, access, method_names)

    return cls


def _prefix_text(text: str, offset: int) -> str:
    """Source text between the previous member boundary and this member."""
    if offset < 0:
        return ""
    start = max(text.rfind(";", 0, offset), text.rfind("{", 0, offset), text.rfind("}", 0, offset))
    return text[start + 1: offset] if start >= 0 else text[:offset]


def _prop_name(member: str) -> str:
    return member[1:] if member.startswith("_") and len(member) > 1 else member


def _handle_field(cls: ClassDesc, node: dict, text: str, access: str):
    if node.get("storageClass") == "static":
        return
    member = node.get("name")
    if not member:
        return
    type_spelling = (node.get("type") or {}).get("qualType", "")
    attrs = parse_field_attributes(_prefix_text(text, _member_offset(node)))

    if "ignore" in attrs:
        return

    prop = _prop_name(member)
    plan = PropertyPlan(prop_name=prop, member_name=member, type_spelling=type_spelling)

    present_get = "get" in attrs
    present_set = "set" in attrs
    # Default (no get/set attribute): generate both with the member's access.
    if not present_get and not present_set:
        present_get = present_set = True
        get_arg = set_arg = None
    else:
        get_arg = attrs.get("get")
        set_arg = attrs.get("set")

    if present_get:
        _plan_accessor(plan, "get", get_arg, access, cls)
    if present_set:
        _plan_accessor(plan, "set", set_arg, access, cls)

    if plan.getter_kind != "none" or plan.setter_kind != "none":
        cls.properties.append(plan)


def _plan_accessor(plan: PropertyPlan, which: str, arg, member_access: str, cls: ClassDesc):
    """Resolve one accessor per the property rules and, when generating, record
    the inline accessor code."""
    prop, member, ty = plan.prop_name, plan.member_name, plan.type_spelling
    if arg is not None and arg not in ACCESS_KEYWORDS:
        # Manual: bind an existing method, generate nothing.
        method = arg
        access = member_access  # reflection access follows the member by default
        kind = "manual"
    else:
        access = arg if arg in ACCESS_KEYWORDS else member_access
        method = f"{which}_{prop}"
        kind = "generate"
        if which == "get":
            code = f"\t{ty} {method}() const {{ return {member}; }}"
        else:
            code = f"\tvoid {method}({ty} value) {{ {member} = std::move(value); }}"
        cls.gen_getters.append((access, code))

    if which == "get":
        plan.getter_kind, plan.getter_access, plan.getter_method = kind, access, method
    else:
        plan.setter_kind, plan.setter_access, plan.setter_method = kind, access, method


def _handle_method(cls: ClassDesc, node: dict, text: str, access: str, method_names: dict):
    name = node.get("name", "")
    if not name or name.startswith("operator"):
        return
    if node.get("isImplicit"):
        return
    if node.get("storageClass") == "static":
        return  # static methods are not auto-bound (would need bind_static_method)
    # Skip templated methods (a template method decl carries inner TemplateType...).
    # We approximate by skipping overloaded names, which also covers most templates.
    if method_names.get(name, 0) > 1:
        return

    attrs = parse_field_attributes(_prefix_text(text, _member_offset(node)))
    if "ignore" in attrs:
        return
    forced = "method" in attrs

    if cls.explicit_methods:
        if not forced:
            return
    else:
        # opt-out: auto-bind public methods; non-public only when forced.
        if access != "public" and not forced:
            return

    cls.methods.append(MethodPlan(name=name, access=access, strict=forced))


# --------------------------------------------------------------------------- #
# Emission
# --------------------------------------------------------------------------- #

def file_id(header: Path, project_root: Path) -> str:
    try:
        rel = header.resolve().relative_to(project_root).as_posix()
    except ValueError:
        rel = header.name
    return "feather_" + re.sub(r"[^0-9a-zA-Z]", "_", rel)


def generate_gen_header(classes: list, header: Path, project_root: Path) -> str:
    fid = file_id(header, project_root)
    any_singleton = any(c.is_singleton for c in classes)

    lines = [GENERATED_NOTICE, "#pragma once", "",
             "#include <framework/static_string.hpp>",
             "#include <utility>"]
    if any_singleton:
        lines.append("#include <framework/singleton_helpers.h>")
    lines += ["",
              "#undef CURRENT_FILE_ID",
              f"#define CURRENT_FILE_ID {fid}",
              ""]

    for c in classes:
        macro = f"{fid}_{c.fclass_line}_GEN_BODY"
        body = []
        body.append("friend class ClassDB;")
        body.append("struct _class_type {};")
        body.append("template <class T> friend void has_bind_method(const T& t);")
        if c.is_singleton:
            body.append(f"FDECLARE_SINGLETON({c.name});")
        body.append("public:")
        body.append(f'\tconstexpr static StaticString get_class_static() {{ return "{c.name}"_ss; }}')
        body.append(f'\tconstexpr static StaticString get_parent_name() {{ return "{c.parent}"_ss; }}')
        body.append("\tbool is_of_type(StaticString type_name) const override "
                    "{ return get_class_static() == type_name || Super::is_of_type(type_name); }")
        body.append("\tvirtual StaticString get_class_name() override { return get_class_static(); }")
        body.append("protected:")
        body.append(f"\tusing Type = {c.name};")
        body.append(f"\tusing Super = {c.parent};")
        body.append("\tstatic void _bind_members();")

        # Generated accessors, grouped by access level.
        for acc in ("public", "protected", "private"):
            group = [code for (a, code) in c.gen_getters if a == acc]
            if group:
                body.append(f"{acc}:")
                body.extend(group)
        body.append("private:")

        # Emit as a single function-like macro with line continuations.
        lines.append(f"#define {macro}() \\")
        lines.append(" \\\n".join(body))
        lines.append("")

    return "\n".join(lines) + "\n"


def _bind_members_body(c: ClassDesc) -> list:
    out = [f"void {c.name}::_bind_members() {{"]
    for p in c.properties:
        ga = ACCESS_ENUM[p.getter_access]
        sa = ACCESS_ENUM[p.setter_access]
        has_get = p.getter_kind != "none"
        has_set = p.setter_kind != "none"
        if has_get and has_set:
            out.append(f"\tClassDB::bind_property_accessors_if_bindable("
                       f"&{c.name}::{p.getter_method}, &{c.name}::{p.setter_method}, "
                       f'"{p.prop_name}", {ga}, {sa});')
        elif has_get:
            out.append(f"\tClassDB::bind_property_get_if_bindable("
                       f'&{c.name}::{p.getter_method}, "{p.prop_name}", {ga});')
        elif has_set:
            out.append(f"\tClassDB::bind_property_set_if_bindable("
                       f'&{c.name}::{p.setter_method}, "{p.prop_name}", {sa});')
    for m in c.methods:
        acc = ACCESS_ENUM[m.access]
        bind = "bind_method" if m.strict else "bind_method_if_bindable"
        out.append(f'\tClassDB::{bind}(&{c.name}::{m.name}, "{m.name}", {acc});')
    out.append("}")
    return out


def topological_sort(classes: list) -> list:
    known = {c.name: c for c in classes}
    result, visited = [], set()

    def visit(c: ClassDesc):
        if c.name in visited:
            return
        visited.add(c.name)
        if c.parent in known:
            visit(known[c.parent])
        result.append(c)

    for c in classes:
        visit(c)
    return result


def generate_register_cpp(subfolder: str, classes: list, core_path: Path) -> str:
    func = f"register_{subfolder}_types"
    ordered = topological_sort(classes)

    includes, seen = [], set()
    for c in ordered:
        rel = _relative_include(c.header, core_path)
        if rel not in seen:
            seen.add(rel)
            includes.append(rel)

    lines = [GENERATED_NOTICE,
             f'#include "register_{subfolder}_types.gen.h"',
             "#include <core/main/class_db.h>",
             ""]
    for inc in includes:
        lines.append(f'#include "{inc}"')
    lines += ["", "namespace feather {", ""]

    for c in ordered:
        lines.extend(_bind_members_body(c))
        lines.append("")

    lines.append(f"void {func}() {{")
    if ordered:
        for c in ordered:
            reg = "register_abstract_class" if c.is_abstract else "register_class"
            lines.append(f"\tClassDB::{reg}<{c.name}>();")
    else:
        lines.append("\t// No reflected classes found in this module.")
    lines += ["}", "", "} // namespace feather", ""]
    return "\n".join(lines) + "\n"


def generate_register_header(subfolder: str) -> str:
    func = f"register_{subfolder}_types"
    return (f"{GENERATED_NOTICE}#pragma once\n\nnamespace feather {{\n\n"
            f"void {func}();\n\n}} // namespace feather\n")


def _relative_include(header: Path, core_path: Path) -> str:
    try:
        return header.resolve().relative_to(core_path.resolve()).as_posix()
    except ValueError:
        return header.name


# --------------------------------------------------------------------------- #
# Driver
# --------------------------------------------------------------------------- #

def write_if_changed(path: Path, content: str) -> bool:
    if path.exists() and path.read_text(encoding="utf-8") == content:
        return False
    path.write_text(content, encoding="utf-8")
    return True


def scan_fclass_occurrences(text: str) -> list:
    """Return [(offset, line, raw_args), ...] for each FCLASS(...) in the text,
    skipping the macro definition lines in reflection_macros.h."""
    out = []
    for m in _FCLASS_RE.finditer(text):
        # skip "#define FCLASS(...)" style definition lines
        line_start = text.rfind("\n", 0, m.start()) + 1
        if "#define" in text[line_start:m.start()]:
            continue
        line = text.count("\n", 0, m.start()) + 1
        out.append((m.start(), line, m.group(1)))
    return out


def process_header(header: Path, clang: str, includes: list, extra_args: list, project_root: Path) -> list:
    text = header.read_text(encoding="utf-8", errors="replace")
    occ = scan_fclass_occurrences(text)
    if not occ:
        return []
    ast = run_clang_ast(clang, header, includes, extra_args)
    target = str(header.resolve())
    records: list = []
    # The TU's own file marker starts as the main file path clang was given.
    collect_records(ast, target, target, records)
    # clang may report the file as the path we passed (not resolved); match both.
    if not records:
        collect_records(ast, str(header), str(header), records)
    return [build_class(r, header, text, occ) for r in records]


def find_headers(folder: Path):
    return sorted(folder.rglob("*.h")) + sorted(folder.rglob("*.hpp"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core-path", type=Path, default=Path("./core"))
    ap.add_argument("--project-root", type=Path, default=Path("."))
    ap.add_argument("--clang", default="clang", help="path to the clang executable")
    ap.add_argument("-I", "--include", action="append", default=[], dest="includes",
                    help="include directory passed to clang (repeatable)")
    ap.add_argument("--clang-arg", action="append", default=[], dest="clang_args",
                    help="extra argument forwarded to clang (repeatable)")
    ap.add_argument("--module-path", action="append", default=[], dest="module_paths",
                    help="additional (non-core) source dir to scan (repeatable)")
    args = ap.parse_args()

    core_path = args.core_path.resolve()
    project_root = args.project_root.resolve()
    includes = [Path(i).resolve() for i in args.includes] or [project_root, core_path]

    if not core_path.is_dir():
        print(f"[ERROR] core path not found: {core_path}", file=sys.stderr)
        sys.exit(1)

    subfolders = [e.name for e in sorted(core_path.iterdir()) if e.is_dir()]

    total_changed = 0
    for sub in subfolders:
        folder = core_path / sub
        classes_by_header: dict = {}
        all_classes: list = []
        for header in find_headers(folder):
            try:
                classes = process_header(header, args.clang, includes, args.clang_args, project_root)
            except Exception as exc:  # keep going; report the offending header
                print(f"  [WARN] {header}: {exc}", file=sys.stderr)
                continue
            if classes:
                classes_by_header[header] = classes
                all_classes.extend(classes)

        # Per-header gen.h files
        for header, classes in classes_by_header.items():
            gen_h = header.with_name(header.stem + ".gen.h")
            if write_if_changed(gen_h, generate_gen_header(classes, header, project_root)):
                total_changed += 1
                print(f"  [{sub}] updated {gen_h.name}")

        # Per-subfolder register files (always emitted so the engine has the symbol)
        out_h = folder / f"register_{sub}_types.gen.h"
        out_cpp = folder / f"register_{sub}_types.gen.cpp"
        if write_if_changed(out_h, generate_register_header(sub)):
            total_changed += 1
            print(f"  [{sub}] updated {out_h.name}")
        if write_if_changed(out_cpp, generate_register_cpp(sub, all_classes, core_path)):
            total_changed += 1
            print(f"  [{sub}] updated {out_cpp.name}")

    print(f"Done ({total_changed} file(s) updated).")


if __name__ == "__main__":
    main()
