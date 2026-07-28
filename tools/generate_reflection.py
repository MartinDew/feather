#!/usr/bin/env python3
"""
generate_reflection.py — FeatherEngine reflection code generator.

Replaces the old regex-based generate_core_registers.py. For every reflected
class (one that uses the FCLASS(...) macro) this tool:

  * recovers the class name and parent from the C++ declaration,
  * extracts member variables (with C++ accessibility) and their [[attributes]],
  * extracts bindable methods (opt-in per class via [[method]]),
  * emits a per-header "<name>.gen.h" holding the Unreal-style GEN_BODY macro
    (reflection boilerplate + generated getters/setters + optional singleton
    boilerplate + the _bind_members() declaration), and
  * emits, per top-level subfolder, a "register_<sub>_types.gen.{h,cpp}" that
    defines every _bind_members() body and the register_<sub>_types() entry
    point the engine calls manually.

Parsing is purely syntactic (Python stdlib `re`, no external process, no
compiler, no pip package) — reflection is opt-in (a member/method is only ever
inspected once it carries a [[get]]/[[set]]/[[name]]/[[method]] attribute), so
the tool never needs to resolve what a type or base class actually means, only
what it is spelled as in source. That also means it can never emit a type
other than the one written in the header (the old clang-AST backend could
silently degrade an unresolved type to `int` when a header failed to parse —
see REFLECTION_CODEGEN_HANDOFF.md). This is not a general C++ parser: it
assumes well-formed, not-too-exotic C++ (no raw string literals, no digraphs,
no reflected declarations inside preprocessor-conditional branches that differ
in shape from the compiled branch).

All writes go through write_if_changed(), so unchanged files keep their mtime
and xmake performs no needless rebuilds.
"""

import argparse
import re
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

# `class Name ... {` / `struct Name ... {`, optionally `final`, optionally a
# base-clause. Only matches an actual *definition* (requires the opening `{`
# immediately after an optional base clause) — a forward declaration
# ("class Foo;") never matches since nothing between the name and the `{`
# that base-clause group would need to stop at is present.
_CLASS_DEF_RE = re.compile(
    r"\b(class|struct)\s+(\w+)\b\s*(?:final\s*)?(?::\s*([^{;]*))?\{"
)

_ACCESS_SPEC_RE = re.compile(r"\s*(public|protected|private)\s*:")

_QUALIFIER_RE = re.compile(r"^(?:static|mutable|inline|constexpr)\b\s*")


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
    name: str               # C++ method name (used for &T::name)
    bind_name: str          # reflected name (defaults to the C++ name)
    access: str             # public / protected / private
    strict: bool            # true when force-bound via [[method]] (hard error if unbindable)
    is_static: bool = False


@dataclass
class ClassDesc:
    name: str
    parent: str
    header: Path
    fclass_line: int
    modifiers: set = dc_field(default_factory=set)
    is_singleton: bool = False
    is_abstract: bool = False
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
# Syntactic source scanning
# --------------------------------------------------------------------------- #

class ParseError(RuntimeError):
    """Raised for a source shape the scanner cannot make sense of, only once an
    [[...]] attribute says the declaration in question is actually meant to be
    reflected — an annotated member/method that can't be parsed must fail loud
    (a silently-skipped class/property/method is exactly the kind of bug that
    made the old clang backend's silent 'int' fallback so painful; see
    REFLECTION_CODEGEN_HANDOFF.md)."""


def _line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def blank_comments_and_literals(text: str) -> str:
    """Return a same-length copy of text with `//` / `/* */` comment bodies and
    string/char literal bodies replaced by spaces (newlines preserved), so
    every offset and line number computed against the result still lines up
    exactly with the original source. Downstream scanning never needs to special
    -case comments again (in particular this is what stops "// FCLASS() plain
    reflected class" in reflection_macros.h's own doc-comment from ever being
    mistaken for a real occurrence). Doesn't understand raw string literals
    (R"(...)") — none of the reflected headers use them."""
    out = []
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if two == "/*":
            out.append("  ")
            i += 2
            while i < n and text[i:i + 2] != "*/":
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
            continue
        c = text[i]
        if c in ("\"", "'"):
            out.append(" ")
            i += 1
            while i < n and text[i] != c:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i < n:
                out.append(" ")
                i += 1
            continue
        out.append(c)
        i += 1
    return "".join(out)


def _find_matching_brace(text: str, open_pos: int) -> int:
    """text[open_pos] must be '{'. Returns the index of the matching '}'."""
    depth = 0
    i, n = open_pos, len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        i += 1
    raise ParseError(f"unbalanced braces starting at offset {open_pos} (line {_line_of(text, open_pos)})")


def find_class_bodies(text: str):
    """Yields (name, parent, tag, body_start, body_end, decl_line) for every
    class/struct *definition* at file or namespace scope. A class nested inside
    another class/struct body is not yielded separately — scanning resumes
    right after a match's closing brace, so a reflected class's own body is
    never re-entered looking for more top-level classes (mirrors the old
    clang-backed collect_records(), which stopped recursing once it found an
    enclosing record: a class nested inside a reflected class was never itself
    a candidate)."""
    pos, n = 0, len(text)
    while pos < n:
        m = _CLASS_DEF_RE.search(text, pos)
        if not m:
            return
        tag, name, base_clause = m.group(1), m.group(2), m.group(3)
        open_pos = m.end() - 1
        close_pos = _find_matching_brace(text, open_pos)
        yield name, _parse_base(base_clause or ""), tag, open_pos + 1, close_pos, _line_of(text, m.start())
        pos = close_pos + 1


def _parse_base(base_clause: str) -> str:
    """First base only (mirrors the old _base_name()); strips access/virtual
    keywords, template args, and namespace qualification down to a simple
    name."""
    if not base_clause.strip():
        return ""
    first = _split_top_level(base_clause)[0]
    tokens = [t for t in first.split() if t not in ("public", "protected", "private", "virtual")]
    name = " ".join(tokens)
    return name.split("<")[0].strip().split("::")[-1]


def iter_member_chunks(body: str):
    """Yields ("access", level) for each access specifier and ("member", text,
    offset) for each other depth-0 member chunk in body (body is class-body
    text, strictly between the class's { and }; offset is relative to body's
    own start). A member chunk is the text up to a depth-0 ';', or up to (and
    including a ';' immediately following, if any) the matching '}' of a
    depth-0 brace block — this covers plain declarations, inline method
    bodies, and default member initializers that themselves contain balanced
    (), [], {} (e.g. `Color _c = Color(1.0f, 1.0f, 1.0f, 1.0f);`). A nested
    class/struct definition is swallowed whole into a single opaque chunk by
    the same rule (its '{' opens depth 1 same as any other brace block) — its
    members, including any [[...]] they carry, are never seen as this class's
    own members, since only a chunk's *leading* attribute is ever read (see
    classify_chunk)."""
    n = len(body)
    pos = 0
    while pos < n:
        m = _ACCESS_SPEC_RE.match(body, pos)
        if m:
            yield "access", m.group(1)
            pos = m.end()
            continue
        depth = 0
        i = pos
        end = None
        while i < n:
            c = body[i]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
                if depth == 0 and c == "}":
                    j = i + 1
                    while j < n and body[j] in " \t\r\n":
                        j += 1
                    if j < n and body[j] == ";":
                        j += 1
                    end = j
                    break
            elif c == ";" and depth == 0:
                end = i + 1
                break
            i += 1
        if end is None:
            end = n
        chunk = body[pos:end]
        if chunk.strip():
            yield "member", chunk, pos
        pos = max(end, pos + 1)


def _strip_leading_attrs(chunk: str):
    """Returns (attr_text, remainder) — remainder starts at the first token
    after all leading [[...]] attribute groups (there may be more than one,
    e.g. `[[get(public)]] [[nodiscard]]`, though this codebase only ever uses
    one group per member)."""
    attr_text = ""
    rest = chunk.lstrip()
    while rest.startswith("[["):
        m = _ATTR_RE.match(rest)
        if not m:
            break
        attr_text += rest[:m.end()]
        rest = rest[m.end():].lstrip()
    return attr_text, rest


_NON_MEMBER_LEAD_WORDS = {"friend", "using", "typedef", "static_assert", "template", "enum"}
_IDENT_RE = re.compile(r"[A-Za-z_]\w*")


def classify_chunk(chunk: str):
    """Parses the leading [[...]] attributes off a member chunk (see
    iter_member_chunks) and classifies the rest. Returns a dict:
      {"attrs": {...}, "kind": "method"|"field"|None, ...kind-specific keys}
    kind is None for declarations that are never reflectable members (friend/
    using/typedef/static_assert/template/enum, or an empty/garbage chunk) —
    the caller skips those outright, attrs or not."""
    attr_text, decl = _strip_leading_attrs(chunk)
    attrs = parse_field_attributes(attr_text)
    decl = decl.strip()
    if decl.endswith(";"):
        decl = decl[:-1].rstrip()

    fw = _IDENT_RE.match(decl)
    if not decl or (fw and fw.group(0) in _NON_MEMBER_LEAD_WORDS):
        return {"attrs": attrs, "kind": None}

    # First depth-0 '(' or '=' decides method vs. field: a '(' must be checked
    # *before* the generic depth-increment for "([{", or it would always be
    # swallowed by that branch first and never recorded (paren_pos would stay
    # None forever, silently misclassifying every method as a field).
    depth = 0
    paren_pos = eq_pos = None
    for idx, c in enumerate(decl):
        if c == "(":
            if depth == 0:
                paren_pos = idx
                break
            depth += 1
        elif c in "[{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "=" and depth == 0:
            eq_pos = idx
            break

    if paren_pos is not None:
        prefix = decl[:paren_pos]
        if re.search(r"\boperator\b", prefix):
            return {"attrs": attrs, "kind": None}
        idents = list(_IDENT_RE.finditer(prefix))
        if not idents:
            return {"attrs": attrs, "kind": None}
        name_m = idents[-1]
        name = name_m.group(0)
        is_dtor = prefix[:name_m.start()].rstrip().endswith("~")
        is_static = bool(re.search(r"(?<!\w)static(?!\w)", prefix[:name_m.start()]))
        return {"attrs": attrs, "kind": "method", "name": name,
                "is_static": is_static, "is_dtor": is_dtor}

    region = decl if eq_pos is None else decl[:eq_pos]
    region = region.rstrip()
    idents = list(_IDENT_RE.finditer(region))
    if not idents:
        return {"attrs": attrs, "kind": None}
    name_m = idents[-1]
    member = name_m.group(0)
    type_spelling = region[:name_m.start()].strip()
    is_static = bool(re.search(r"(?<!\w)static(?!\w)", type_spelling))
    while True:
        stripped = _QUALIFIER_RE.sub("", type_spelling)
        if stripped == type_spelling:
            break
        type_spelling = stripped
    type_spelling = re.sub(r"\s+", " ", type_spelling).strip()
    return {"attrs": attrs, "kind": "field", "name": member,
            "type": type_spelling, "is_static": is_static}


# --------------------------------------------------------------------------- #
# Building the class descriptor from a parsed body + raw source
# --------------------------------------------------------------------------- #

def build_class(name: str, parent: str, tag: str, body: str, header: Path, fclass_index: list,
                 body_start: int, body_end: int) -> "ClassDesc | None":
    """Returns None if no FCLASS(...) occurrence falls inside [body_start,
    body_end) — find_class_bodies() finds every class/struct definition in the
    file, reflected or not, so this is what stops a plain helper struct
    sharing a header with a real FCLASS class from being reflected too."""
    modifiers, fclass_line, matched = set(), 0, False
    for (off, line, args) in fclass_index:
        if body_start <= off <= body_end:
            modifiers = {a.strip() for a in args.split(",") if a.strip()}
            fclass_line = line
            matched = True
            break
    if not matched:
        return None

    cls = ClassDesc(
        name=name, parent=parent, header=header, fclass_line=fclass_line,
        modifiers=modifiers,
        is_singleton="singleton" in modifiers,
        is_abstract="abstract" in modifiers,
    )

    # First pass: tally method names (constructors/destructors/operators
    # excluded, matching the old clang backend's CXXMethodDecl-only tally) so
    # overloaded names can be excluded from binding below — &T::name would be
    # ambiguous for an overload, whether or not the overload itself is
    # annotated.
    method_names: dict = {}
    for item in iter_member_chunks(body):
        if item[0] != "member":
            continue
        info = classify_chunk(item[1])
        if info["kind"] == "method" and not info["is_dtor"]:
            n = info["name"]
            if n != name:  # constructor
                method_names[n] = method_names.get(n, 0) + 1

    default_access = "public" if tag == "struct" else "private"
    current_access = default_access

    for item in iter_member_chunks(body):
        if item[0] == "access":
            current_access = item[1]
            continue
        _, chunk, offset = item
        info = classify_chunk(chunk)
        attrs = info["attrs"]
        kind = info["kind"]
        if kind is None:
            if attrs:
                raise ParseError(
                    f"{header}:{_line_of(body, offset) + fclass_line - 1}: class {name}: "
                    f"couldn't parse an annotated member (attrs={attrs!r}): {chunk.strip()[:120]!r}"
                )
            continue

        if kind == "field":
            _handle_field(cls, info, current_access, header, body, offset, fclass_line)
        elif kind == "method":
            if info["is_dtor"] or info["name"] == name:
                continue  # destructor / constructor, never reflectable
            _handle_method(cls, info, current_access, method_names, header, body, offset, fclass_line)

    return cls


def _prop_name(member: str) -> str:
    return member[1:] if member.startswith("_") and len(member) > 1 else member


def _handle_field(cls: ClassDesc, info: dict, access: str, header: Path, body: str, offset: int, fclass_line: int):
    if info["is_static"]:
        return
    member = info["name"]
    type_spelling = info["type"]
    attrs = info["attrs"]

    if "ignore" in attrs:
        return

    present_get = "get" in attrs
    present_set = "set" in attrs
    # Properties are opt-in: a member reflects only when annotated with at least
    # one of [[get]]/[[set]]/[[name]] (mirrors Unreal's explicit UPROPERTY()).
    # A bare [[name(foo)]] with no get/set still implies both accessors.
    if not present_get and not present_set and "name" not in attrs:
        return

    if not type_spelling:
        raise ParseError(f"{header}: class field '{member}' near line "
                          f"{_line_of(body, offset) + fclass_line - 1}: couldn't recover a type spelling")

    # [[name(foo)]] overrides the reflected property name (default: member without
    # its leading underscore).
    prop = attrs.get("name") or _prop_name(member)
    plan = PropertyPlan(prop_name=prop, member_name=member, type_spelling=type_spelling)

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


def _handle_method(cls: ClassDesc, info: dict, access: str, method_names: dict,
                    header: Path, body: str, offset: int, fclass_line: int):
    name = info["name"]
    attrs = info["attrs"]
    if "ignore" in attrs:
        return
    # Methods are opt-in: only [[method]] (optionally [[method(name)]] to rebind
    # under a custom reflected name) binds a method, whether static or not.
    forced = "method" in attrs
    if not forced:
        return
    # Skip templated/overloaded names: &T::name would be ambiguous. Overloads
    # must be disambiguated by hand rather than auto-bound.
    if method_names.get(name, 0) > 1:
        return
    bind_name = attrs.get("method") or name

    cls.methods.append(MethodPlan(
        name=name, bind_name=bind_name, access=access, strict=forced, is_static=info["is_static"]
    ))


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
        if m.is_static:
            out.append(f'\tClassDB::bind_static_method(&{c.name}::{m.name}, "{m.bind_name}", {acc});')
        else:
            bind = "bind_method" if m.strict else "bind_method_if_bindable"
            out.append(f'\tClassDB::{bind}(&{c.name}::{m.name}, "{m.bind_name}", {acc});')
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
    """Return [(offset, line, raw_args), ...] for each real FCLASS(...) in the
    (already comment/literal-blanked) text, skipping the macro definition line
    in reflection_macros.h. Comment occurrences (e.g. reflection_macros.h's own
    "//   FCLASS()  plain reflected class" doc-comment) never reach here at all
    — blank_comments_and_literals() already erased them before this runs."""
    out = []
    for m in _FCLASS_RE.finditer(text):
        line_start = text.rfind("\n", 0, m.start()) + 1
        prefix = text[line_start:m.start()]
        # skip "#define FCLASS(...)" style definition lines
        if "#define" in prefix:
            continue
        line = text.count("\n", 0, m.start()) + 1
        out.append((m.start(), line, m.group(1)))
    return out


def process_header(header: Path, project_root: Path) -> list:
    # newline="" disables universal-newline translation: on a CRLF checkout (the
    # git default on Windows unless core.autocrlf/.gitattributes forces LF),
    # Path.read_text()'s default translation of "\r\n" -> "\n" would silently
    # collapse one byte per line, making every offset/line number computed here
    # increasingly diverge from the raw on-disk bytes. build_class()'s
    # occurrence-to-body range check then misses classes further down the file
    # -- observed as PBRMaterial (last class in material.h) silently missing
    # its GEN_BODY macro on a CRLF Windows checkout while earlier classes in
    # the same file still resolved. Path.open()'s newline param works on all
    # Python 3 versions, unlike Path.read_text()'s (3.13+ only).
    raw = header.open(encoding="utf-8", errors="replace", newline="").read()
    text = blank_comments_and_literals(raw)
    occ = scan_fclass_occurrences(text)
    if not occ:
        return []

    classes = []
    for name, parent, tag, body_start, body_end, _decl_line in find_class_bodies(text):
        body = text[body_start:body_end]
        cls = build_class(name, parent, tag, body, header, occ, body_start, body_end)
        if cls is not None:
            classes.append(cls)
    return classes


def find_headers(folder: Path):
    return sorted(folder.rglob("*.h")) + sorted(folder.rglob("*.hpp"))


def process_source_dir(dir_path: Path, name: str, include_base: Path, project_root: Path) -> int:
    """Scan dir_path recursively for FCLASS headers, emit each header's .gen.h,
    and emit dir_path/register_<name>_types.gen.{h,cpp}. Shared by the per-core-
    subfolder loop and the --module-path loop in main() -- a module directory is
    processed exactly like a core subfolder, just outside core/ and named after
    itself rather than a core subfolder name. include_base is what the generated
    #include paths in register_<name>_types.gen.cpp are computed relative to
    (core_path for core subfolders, the module dir itself for modules, so e.g.
    "vex_renderer.h" rather than "modules/vex_renderer/vex_renderer.h"). Returns
    the number of files written."""
    changed = 0
    classes_by_header: dict = {}
    all_classes: list = []
    for header in find_headers(dir_path):
        try:
            classes = process_header(header, project_root)
        except ParseError as exc:  # an annotated declaration we couldn't parse -- fatal
            print(f"[ERROR] {exc}", file=sys.stderr)
            sys.exit(1)
        if classes:
            classes_by_header[header] = classes
            all_classes.extend(classes)

    # Per-header gen.h files
    for header, classes in classes_by_header.items():
        gen_h = header.with_name(header.stem + ".gen.h")
        if write_if_changed(gen_h, generate_gen_header(classes, header, project_root)):
            changed += 1
            print(f"  [{name}] updated {gen_h.name}")

    # register_<name>_types.gen.{h,cpp} (always emitted so the caller has the symbol)
    out_h = dir_path / f"register_{name}_types.gen.h"
    out_cpp = dir_path / f"register_{name}_types.gen.cpp"
    if write_if_changed(out_h, generate_register_header(name)):
        changed += 1
        print(f"  [{name}] updated {out_h.name}")
    if write_if_changed(out_cpp, generate_register_cpp(name, all_classes, include_base)):
        changed += 1
        print(f"  [{name}] updated {out_cpp.name}")
    return changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core-path", type=Path, default=Path("./core"))
    ap.add_argument("--project-root", type=Path, default=Path("."))
    ap.add_argument("--module-path", action="append", default=[], dest="module_paths",
                    help="additional (non-core) source dir to scan (repeatable)")
    ap.add_argument("--skip-core", action="store_true",
                    help="don't scan --core-path; only process --module-path dirs. "
                         "For manually refreshing a single module's own generated "
                         "files without re-scanning all of core. NOT used by the "
                         "xmake wiring (xmake/modules/feather_codegen.lua): a "
                         "module's headers transitively include core headers that "
                         "need their own .gen.h too, and a module target's files "
                         "may compile before the main executable's before_build has "
                         "produced core's, so both the module's own before_build and "
                         "the executable's must generate core in full.")
    args = ap.parse_args()

    core_path = args.core_path.resolve()
    project_root = args.project_root.resolve()

    total_changed = 0

    if not args.skip_core:
        if not core_path.is_dir():
            print(f"[ERROR] core path not found: {core_path}", file=sys.stderr)
            sys.exit(1)
        subfolders = [e.name for e in sorted(core_path.iterdir()) if e.is_dir()]
        for sub in subfolders:
            folder = core_path / sub
            total_changed += process_source_dir(folder, sub, core_path, project_root)

    for mp in args.module_paths:
        mod_path = Path(mp).resolve()
        if not mod_path.is_dir():
            print(f"[WARN] module path not found: {mod_path}", file=sys.stderr)
            continue
        # Named after the directory itself (e.g. "vex_renderer"), producing
        # modules/vex_renderer/register_vex_renderer_types.gen.{h,cpp} -- the
        # module's own xmake.lua adds that cpp to its file list, same idea as
        # GENERATED_SOURCE does for core's register_<sub>_types.gen.cpp.
        total_changed += process_source_dir(mod_path, mod_path.name, mod_path, project_root)

    print(f"Done ({total_changed} file(s) updated).")


if __name__ == "__main__":
    main()
