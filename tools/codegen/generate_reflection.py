#!/usr/bin/env python3
"""
generate_reflection.py — FeatherEngine reflection code generator.

For every reflected class (uses FCLASS(...)/FSTRUCT(...)) this tool recovers
the name/parent from the C++ declaration, extracts [[attribute]]-annotated
members and methods, emits a per-header "<name>.gen.h" (Unreal-style GEN_BODY
macro: reflection boilerplate + generated accessors + _bind_members() decl),
and emits per-subfolder "register_<sub>_types.gen.{h,cpp}" defining every
_bind_members() body plus the register_<sub>_types() entry point.

Parsing is purely syntactic (stdlib `re`, no compiler) and opt-in: a
member/method is only ever inspected once annotated, so the tool never
resolves what a type actually is, only how it's spelled -- it can't silently
degrade an unresolved type to `int` the way the old clang-AST backend could
(see REFLECTION_CODEGEN_HANDOFF.md). Not a general C++ parser: assumes
well-formed, not-too-exotic C++ (no raw string literals, no digraphs).

[[get(...)]]/[[set(...)]] accept an access keyword and/or `ref` (e.g.
[[get(protected, ref)]]) for a const-reference accessor instead of by-value;
see _plan_accessor() for why only const-ref (not mutable T&) is supported.

A reflected member may sit inside arbitrarily nested #if/#ifdef/.../#endif;
condition text is only ever copied, never evaluated. See
iter_conditioned_chunks() for the #if-stack tracking and
generate_gen_header()/_bind_members_body() for how that same condition is
reproduced around generated code, so one .gen.h covers every build config.

Writes go through write_if_changed(), so unchanged files keep their mtime.
"""

import argparse
import re
import sys
from dataclasses import dataclass, field as dc_field
from pathlib import Path

# So extension modules' `from modifier_api import ...` resolves the same way
# for built-ins and project extensions, without each needing its own path hack.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from modifier_api import EmitContext, ModifierError, Registry, load_extension_path  # noqa: E402

GENERATED_NOTICE = (
    "// THIS FILE IS AUTO-GENERATED — DO NOT EDIT BY HAND.\n"
    "// Re-run tools/generate_reflection.py to refresh.\n"
)

ACCESS_KEYWORDS = {"public", "protected", "private"}
ACCESS_ENUM = {"public": "AccessLevel::Public", "protected": "AccessLevel::Protected", "private": "AccessLevel::Private"}

# FCLASS(...)/FSTRUCT(...) invocation: macro name + modifier list. \b at both
# ends avoids matching a longer identifier like FCLASS_ANYTHING(.
_FCLASS_RE = re.compile(r"\b(FCLASS|FSTRUCT)\b\s*\(([^)]*)\)")
_ATTR_RE = re.compile(r"\[\[(.*?)\]\]", re.DOTALL)

# `class Name ... {` / `struct Name ... {`, optionally `final`, optionally a
# base-clause. Requires the opening `{` right after, so a forward declaration
# ("class Foo;") never matches.
_CLASS_DEF_RE = re.compile(
    r"\b(class|struct)\s+(\w+)\b\s*(?:final\s*)?(?::\s*([^{;]*))?\{"
)

_ACCESS_SPEC_RE = re.compile(r"\s*(public|protected|private)\s*:")

_QUALIFIER_RE = re.compile(r"^(?:static|mutable|inline|constexpr)\b\s*")

# A preprocessor directive: '#' at the start of a logical line -- see iter_member_chunks.
_PP_KEYWORD_RE = re.compile(r"[ \t]*#[ \t]*(\w+)")
_PP_CONDITIONAL_KEYWORDS = {"if", "ifdef", "ifndef", "elif", "else", "endif"}


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
    condition: str = None   # verbatim (flattened) #if expression guarding this member, if any


@dataclass
class MethodPlan:
    name: str               # C++ method name (used for &T::name)
    bind_name: str          # reflected name (defaults to the C++ name)
    access: str             # public / protected / private
    strict: bool            # true when force-bound via [[method]] (hard error if unbindable)
    is_static: bool = False
    condition: str = None   # verbatim (flattened) #if expression guarding this member, if any


@dataclass
class ClassDesc:
    name: str
    parent: str
    header: Path
    fclass_line: int
    tag: str = "class"      # "class" or "struct", as the type was declared
    modifiers: set = dc_field(default_factory=set)   # raw tokens, e.g. {"singleton", "System(A, B)"}
    # list[(Modifier, arg_text_or_None)], resolved against the dir's Registry
    # by build_class -- see modifier_api.Registry.resolve. singleton/abstract
    # are no longer special fields: a modifier's presence (and its
    # register_statements()/gen_body_lines() hooks) is the single source of
    # truth, same as any other modifier.
    resolved_modifiers: list = dc_field(default_factory=list)
    # A value type is reflected WITHOUT any virtual function: no vtable pointer,
    # so it stays trivially small (the point of reflecting an ECS component).
    # Set by FSTRUCT(...) or by FCLASS(novtable) — never inferred from the base
    # clause: a purely syntactic scanner can't tell whether a parent spelled in
    # another header eventually reaches Reflected, so the declaration site says
    # so explicitly. See generate_gen_header() for what is left out.
    is_value_type: bool = False
    properties: list = dc_field(default_factory=list)     # list[PropertyPlan]
    gen_getters: list = dc_field(default_factory=list)    # (access, code, condition) inline accessor defs
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
    {"ignore": None}. A token's parenthesised argument is kept as the raw,
    still-comma-joined text (e.g. "public, ref") -- most callers only ever
    have one word there and can keep treating the value as a plain string,
    but [[get(...)]]/[[set(...)]] split it further themselves (see
    _plan_accessor) to allow combining an access keyword with `ref`.
    """
    tokens: dict = {}
    for group in _ATTR_RE.findall(prefix_text):
        for tok in _split_top_level(group):
            m = re.match(r"^(\w+)\s*(?:\(\s*(.*?)\s*\))?$", tok, re.DOTALL)
            if not m:
                continue
            tokens[m.group(1)] = m.group(2)  # arg text or None
    return tokens


# --------------------------------------------------------------------------- #
# Syntactic source scanning
# --------------------------------------------------------------------------- #

class ParseError(RuntimeError):
    """Raised for a source shape the scanner can't make sense of, only once an
    [[...]] attribute marks the declaration as meant to be reflected -- fails
    loud rather than silently skipping, unlike the old clang backend's 'int'
    fallback (see REFLECTION_CODEGEN_HANDOFF.md)."""


def _line_of(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def blank_comments_and_literals(text: str) -> str:
    """Return a same-length copy of text with comment/string/char literal
    bodies blanked to spaces (newlines preserved), so offsets/line numbers
    still line up with the original and downstream scanning never has to
    special-case comments (e.g. reflection_macros.h's own doc-comment
    mentioning "FCLASS()" can't be mistaken for a real occurrence). Doesn't
    understand raw string literals (R"(...)") -- unused in reflected headers."""
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
    class/struct *definition* at file or namespace scope. A nested class isn't
    yielded separately -- scanning resumes right after a match's closing brace
    (mirrors the old clang backend, which never recursed into a found record)."""
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


def _logical_line_end(body: str, start: int) -> int:
    """Returns the index just past the end of the (possibly backslash-newline
    -spliced) logical line starting at start -- i.e. past the first '\\n' that
    isn't itself preceded by a line-continuing '\\' (optionally through a
    '\\r'), so a long #if condition spread over several physical lines is
    still treated as one directive."""
    n = len(body)
    i = start
    while True:
        nl = body.find("\n", i)
        if nl == -1:
            return n
        j = nl - 1
        if j >= i and body[j] == "\r":
            j -= 1
        if j >= i and body[j] == "\\":
            i = nl + 1
            continue
        return nl + 1


def iter_member_chunks(body: str):
    """Yields ("access", level) for each access specifier, ("pp", keyword,
    rest) for each preprocessor directive line, and ("member", text, offset)
    for each other depth-0 member chunk in body (class-body text strictly
    between { and }; offset relative to body's own start).

    A directive is consumed as its own zero-content unit *before* the member-
    chunk scan reaches it -- otherwise it would glue onto whatever follows
    into one garbled chunk, silently losing a leading [[...]] attribute
    (classify_chunk only reads a *leading* attribute) and desyncing the
    access-level tracking if an access specifier falls in the same span.
    keyword/rest feed iter_conditioned_chunks' #if-stack; this function has no
    opinion on what the directives mean.

    A member chunk runs to a depth-0 ';', or through the matching '}' of a
    depth-0 brace block (plus a trailing ';' if present) -- covers plain
    declarations, inline method bodies, and initializers with balanced (){}[]
    (e.g. `Color _c = Color(1.0f, 1.0f, 1.0f, 1.0f);`). A nested class/struct
    is swallowed whole by the same rule, so its members are never seen as this
    class's own. Directives *inside* a single declaration aren't supported --
    not worth the complexity for code that doesn't do this."""
    n = len(body)
    pos = 0
    while pos < n:
        while pos < n and body[pos] in " \t\r\n":
            pos += 1
        if pos >= n:
            return
        if body[pos] == "#":
            m = _PP_KEYWORD_RE.match(body, pos)
            end = _logical_line_end(body, pos)
            keyword = m.group(1) if m else ""
            rest = body[m.end():end] if m else ""
            rest = re.sub(r"\\\r?\n", " ", rest)
            rest = re.sub(r"\s+", " ", rest).strip()
            yield "pp", keyword, rest
            pos = end
            continue
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


@dataclass
class _CondFrame:
    """One #if/#ifdef/#ifndef...#endif nesting level. chain is a unique id per
    opening directive, shared by every #elif/#else that follows (so sibling
    branches can be recognized as mutually exclusive); branch counts up from 0.
    own_text is this branch's normalized condition (#ifdef X -> "defined(X)",
    etc.) -- None for #else, whose condition is only known once every prior
    sibling's text is collected (see effective_text)."""
    chain: int
    branch: int = 0
    prior_texts: list = dc_field(default_factory=list)
    own_text: str = None

    def effective_text(self) -> str:
        if self.own_text is not None:
            return self.own_text
        # #else: true exactly when none of the prior branches' conditions held.
        return " && ".join(f"!({t})" for t in self.prior_texts) if self.prior_texts else "1"


def _normalize_directive_condition(keyword: str, rest: str) -> str:
    if keyword == "ifdef":
        return f"defined({rest})"
    if keyword == "ifndef":
        return f"!defined({rest})"
    return rest  # if / elif: rest is already a plain boolean expression


def _mutually_exclusive(path_a: tuple, path_b: tuple) -> bool:
    """True if path_a and path_b (tuples of (chain, branch), outermost first,
    from iter_conditioned_chunks) are *provably* mutually exclusive: nested
    identically up to some #if-chain, then landing in different branches of it
    (the "#if X ... #else ... #endif" duplicate-impl idiom). Only that direct-
    sibling pattern is recognized -- independent conditions in different
    chains are conservatively NOT treated as exclusive, and two empty paths
    (unconditional code) compare as not-exclusive, matching the old
    "duplicate name -> ambiguous, skip" behavior."""
    for (chain_a, branch_a), (chain_b, branch_b) in zip(path_a, path_b):
        if chain_a != chain_b:
            break
        if branch_a != branch_b:
            return True
    return False


def iter_conditioned_chunks(body: str):
    """Wraps iter_member_chunks(), tracking a stack of active #if/#ifdef/
    #ifndef/#elif/#else regions. Yields:
      ("access", level)
      ("member", chunk_text, offset, condition, cond_path)
    condition is None when unconditional, else the flattened, fully-
    parenthesized '(A) && (B)' text of every enclosing region, verbatim and
    never interpreted. Each #if/#ifdef/#ifndef opens a frame; #elif/#else
    advance the top frame in place; #endif closes it. Raises ParseError on an
    unbalanced #if/#endif. cond_path is that stack reduced to (chain, branch)
    pairs, used by build_class() to spot same-named methods in mutually
    exclusive #if branches (see _mutually_exclusive)."""
    stack: list = []
    chain_counter = [0]

    for item in iter_member_chunks(body):
        kind = item[0]
        if kind == "pp":
            _, keyword, rest = item
            if keyword not in _PP_CONDITIONAL_KEYWORDS:
                continue  # e.g. a stray #pragma/#define inside a class body
            if keyword in ("if", "ifdef", "ifndef"):
                chain_counter[0] += 1
                stack.append(_CondFrame(chain=chain_counter[0],
                                         own_text=_normalize_directive_condition(keyword, rest)))
            elif keyword in ("elif", "else"):
                if stack:
                    top = stack[-1]
                    top.prior_texts.append(top.effective_text())
                    top.branch += 1
                    top.own_text = _normalize_directive_condition("if", rest) if keyword == "elif" else None
            elif keyword == "endif":
                if stack:
                    stack.pop()
            continue
        if kind == "access":
            yield item
            continue
        _, chunk, offset = item
        condition = " && ".join(f"({f.effective_text()})" for f in stack) if stack else None
        cond_path = tuple((f.chain, f.branch) for f in stack)
        yield "member", chunk, offset, condition, cond_path

    if stack:
        raise ParseError(f"unbalanced #if/#endif inside a class body ({len(stack)} still open)")


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
                 body_start: int, body_end: int, registry: Registry, dir_name: str) -> "ClassDesc | None":
    """Returns None if no FCLASS(...)/FSTRUCT(...) occurrence falls inside
    [body_start, body_end) -- find_class_bodies() finds every class/struct
    definition, reflected or not, so this is what stops a plain helper struct
    sharing a header with a real FCLASS class from being reflected too."""
    modifiers, fclass_line, macro, matched = set(), 0, "FCLASS", False
    for (off, line, args, mac) in fclass_index:
        if body_start <= off <= body_end:
            # _split_top_level, not .split(","), so a modifier taking its own
            # comma-separated args (e.g. System(OnUpdate, Position)) survives intact.
            modifiers = set(_split_top_level(args))
            fclass_line = line
            macro = mac
            matched = True
            break
    if not matched:
        return None

    cls = ClassDesc(
        name=name, parent=parent, header=header, fclass_line=fclass_line,
        tag=tag, modifiers=modifiers,
        is_value_type=(macro == "FSTRUCT" or "novtable" in modifiers),
    )

    def perr(msg: str) -> ParseError:
        return ParseError(f"{header}:{fclass_line}: class {name}: {msg}")

    try:
        resolved = registry.resolve(modifiers)
    except ModifierError as exc:
        raise perr(str(exc)) from None
    ctx = EmitContext(dir_name=dir_name)
    for modifier, _arg in resolved:
        if "class" not in modifier.targets:
            raise perr(f"modifier '{modifier.name}' can't be applied to a class (targets {sorted(modifier.targets)})")
        if modifier.value_type is not None and modifier.value_type != cls.is_value_type:
            want = "a value type (FSTRUCT / FCLASS(novtable))" if modifier.value_type \
                else "a non-value (Reflected-derived) type"
            raise perr(f"modifier '{modifier.name}' requires {want}")
        try:
            modifier.validate(cls, ctx)
        except ModifierError as exc:
            raise perr(f"modifier '{modifier.name}': {exc}") from None
    cls.resolved_modifiers = resolved

    try:
        # First pass: tally method names by (source offset, cond_path),
        # excluding ctors/dtors, so an overloaded name (ambiguous for &T::name)
        # can be excluded from binding below. Keyed by offset so a method can
        # be compared against every *other* same-named occurrence without
        # matching itself; cond_path lets _handle_method recognize the "#if X
        # ... #else ... #endif" duplicate-impl idiom as NOT ambiguous (see _mutually_exclusive).
        method_paths: dict = {}
        for item in iter_conditioned_chunks(body):
            if item[0] != "member":
                continue
            _, chunk, offset, _condition, cond_path = item
            info = classify_chunk(chunk)
            if info["kind"] == "method" and not info["is_dtor"]:
                n = info["name"]
                if n != name:  # constructor
                    method_paths.setdefault(n, []).append((offset, cond_path))

        default_access = "public" if tag == "struct" else "private"
        current_access = default_access

        for item in iter_conditioned_chunks(body):
            if item[0] == "access":
                current_access = item[1]
                continue
            _, chunk, offset, condition, cond_path = item
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
                _handle_field(cls, info, current_access, condition, header, body, offset, fclass_line)
            elif kind == "method":
                if info["is_dtor"] or info["name"] == name:
                    continue  # destructor / constructor, never reflectable
                _handle_method(cls, info, current_access, condition, offset, cond_path, method_paths,
                                header, body, fclass_line)
    except ParseError as exc:
        if str(exc).startswith("unbalanced #if"):
            raise ParseError(f"{header}: class {name}: {exc}") from None
        raise

    return cls


def _prop_name(member: str) -> str:
    return member[1:] if member.startswith("_") and len(member) > 1 else member


def _handle_field(cls: ClassDesc, info: dict, access: str, condition: str,
                   header: Path, body: str, offset: int, fclass_line: int):
    if info["is_static"]:
        return
    member = info["name"]
    type_spelling = info["type"]
    attrs = info["attrs"]

    if "ignore" in attrs:
        return

    present_get = "get" in attrs
    present_set = "set" in attrs
    # Opt-in: reflects only with [[get]]/[[set]]/[[name]]. A bare [[name(foo)]]
    # with no get/set still implies both accessors.
    if not present_get and not present_set and "name" not in attrs:
        return

    if not type_spelling:
        raise ParseError(f"{header}: class field '{member}' near line "
                          f"{_line_of(body, offset) + fclass_line - 1}: couldn't recover a type spelling")

    # [[name(foo)]] overrides the reflected property name (default: member without
    # its leading underscore).
    prop = attrs.get("name") or _prop_name(member)
    plan = PropertyPlan(prop_name=prop, member_name=member, type_spelling=type_spelling, condition=condition)

    if not present_get and not present_set:
        present_get = present_set = True
        get_arg = set_arg = None
    else:
        get_arg = attrs.get("get")
        set_arg = attrs.get("set")

    if present_get:
        _plan_accessor(plan, "get", get_arg, access, condition, cls, header, body, offset, fclass_line)
    if present_set:
        _plan_accessor(plan, "set", set_arg, access, condition, cls, header, body, offset, fclass_line)

    if plan.getter_kind != "none" or plan.setter_kind != "none":
        cls.properties.append(plan)


_REF_KEYWORD = "ref"


def _plan_accessor(plan: PropertyPlan, which: str, arg, member_access: str, condition: str, cls: ClassDesc,
                    header: Path, body: str, offset: int, fclass_line: int):
    """Resolve one accessor per the property rules and, when generating, record
    the inline accessor code. arg is the raw text inside [[get(...)]]/
    [[set(...)]] -- e.g. "public", "ref", "public, ref", or a manual accessor
    name -- or None for a bare [[get]]/[[set]].

    `ref` makes a generated accessor pass/return by const-reference instead of
    by-value (getter returns `const T&`, setter takes `const T&` and
    copy-assigns rather than std::move()s). Only const-ref is supported, not
    mutable T&: the getter must be `const` (bind_property_get(_if_bindable)
    requires it, so it could never actually return a mutable reference), and
    the setter call site in class_db.inl passes `std::move(result.value())`,
    which won't bind to a non-const T& parameter. Both are real compile
    errors, so `ref` unconditionally means const-ref."""
    prop, member, ty = plan.prop_name, plan.member_name, plan.type_spelling

    def err(msg: str):
        return ParseError(f"{header}: class {cls.name} field '{member}' near line "
                           f"{_line_of(body, offset) + fclass_line - 1}: {msg}")

    tokens = _split_top_level(arg) if arg else []
    for tok in tokens:
        if re.search(r"\s", tok):
            raise err(f"[[{which}({arg})]]: multiple arguments must be comma-separated, not "
                      f"space-separated (e.g. '{', '.join(tok.split())}' instead of {tok!r})")
    access_tok = next((t for t in tokens if t in ACCESS_KEYWORDS), None)
    is_ref = _REF_KEYWORD in tokens
    manual_toks = [t for t in tokens if t not in ACCESS_KEYWORDS and t != _REF_KEYWORD]

    if manual_toks:
        if len(manual_toks) > 1:
            raise err(f"[[{which}(...)]] takes at most one manual accessor name, got {manual_toks!r}")
        if access_tok or is_ref:
            raise err(f"[[{which}({arg})]]: can't combine a manual accessor name ({manual_toks[0]!r}) "
                      f"with an access keyword or 'ref' -- those only apply to a generated accessor")
        # Manual: bind an existing method, generate nothing.
        method = manual_toks[0]
        access = member_access  # reflection access follows the member by default
        kind = "manual"
    else:
        access = access_tok or member_access
        method = f"{which}_{prop}"
        kind = "generate"
        if which == "get":
            if is_ref:
                code = f"\tconst {ty}& {method}() const {{ return {member}; }}"
            else:
                code = f"\t{ty} {method}() const {{ return {member}; }}"
        else:
            if is_ref:
                code = f"\tvoid {method}(const {ty}& value) {{ {member} = value; }}"
            else:
                code = f"\tvoid {method}({ty} value) {{ {member} = std::move(value); }}"
        cls.gen_getters.append((access, code, condition))

    if which == "get":
        plan.getter_kind, plan.getter_access, plan.getter_method = kind, access, method
    else:
        plan.setter_kind, plan.setter_access, plan.setter_method = kind, access, method


def _handle_method(cls: ClassDesc, info: dict, access: str, condition: str, offset: int, cond_path: tuple,
                    method_paths: dict, header: Path, body: str, fclass_line: int):
    name = info["name"]
    attrs = info["attrs"]
    if "ignore" in attrs:
        return
    # Opt-in: only [[method]] (or [[method(name)]] to rebind) binds a method.
    forced = "method" in attrs
    if not forced:
        return
    # bind_method dispatches through object_cast<T>(Reflected*), which a
    # vtable-free value type has no way to support; a static method is fine.
    if cls.is_value_type and not info["is_static"]:
        raise ParseError(
            f"{header}: class {cls.name} method '{name}' near line "
            f"{_line_of(body, offset) + fclass_line - 1}: [[method]] on a non-static method of a "
            f"value type (FSTRUCT / FCLASS(novtable)) can't be bound -- ClassDB::bind_method "
            f"dispatches through object_cast<T>(Reflected*), which a vtable-free type has no way "
            f"to support. Make the method static, or drop 'novtable'/use FCLASS instead."
        )
    # Skip overloaded names (&T::name is ambiguous) unless every other
    # same-named occurrence is provably in a mutually exclusive #if branch of
    # the same chain (the "#if X ... #else ... #endif" duplicate-impl idiom).
    others = [p for (o, p) in method_paths.get(name, []) if o != offset]
    if any(not _mutually_exclusive(cond_path, p) for p in others):
        return
    bind_name = attrs.get("method") or name

    cls.methods.append(MethodPlan(
        name=name, bind_name=bind_name, access=access, strict=forced,
        is_static=info["is_static"], condition=condition
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


def generate_gen_header(classes: list, header: Path, project_root: Path, registry: Registry, dir_name: str) -> str:
    fid = file_id(header, project_root)

    extra_includes, seen_inc = [], set()
    plain_ctx = EmitContext(dir_name=dir_name)
    for c in classes:
        for modifier, _arg in c.resolved_modifiers:
            for inc in modifier.gen_header_includes(c, plain_ctx):
                if inc not in seen_inc:
                    seen_inc.add(inc)
                    extra_includes.append(inc)

    lines = [GENERATED_NOTICE, "#pragma once", "",
             "#include <framework/static_string.hpp>",
             "#include <utility>"]
    for inc in extra_includes:
        lines.append(f"#include <{inc}>")
    lines += ["",
              "#undef CURRENT_FILE_ID",
              f"#define CURRENT_FILE_ID {fid}",
              ""]
    helper_insert_at = len(lines)

    # #if/#endif can't be spliced into a backslash-continued macro body (line-
    # splicing happens before directives are recognized, so it'd just be
    # literal text, not a directive -- FCLASS() would expand to a syntax
    # error). So a conditional accessor is hoisted into its own top-level
    # macro guarded by a real #if/#else/#endif, and GEN_BODY merely references
    # that macro's bare name, expanded normally on rescan. One .gen.h then
    # covers every build config; no per-config regeneration needed.
    helper_blocks: list = []
    helper_counter = [0]

    def hoist(macro: str, acc: str, code_lines: list, condition: str) -> str:
        helper_counter[0] += 1
        helper_name = f"{macro}_{acc.upper()}_COND{helper_counter[0]}"
        helper_blocks.append(f"#if {condition}")
        helper_blocks.append(f"#define {helper_name} \\")
        helper_blocks.append(" \\\n".join(code_lines))
        helper_blocks.append("#else")
        helper_blocks.append(f"#define {helper_name}")
        helper_blocks.append("#endif")
        helper_blocks.append("")
        return helper_name

    for c in classes:
        macro = f"{fid}_{c.fclass_line}_GEN_BODY"
        cls_ctx = EmitContext(dir_name=dir_name, _hoist=lambda code_lines, cond: hoist(macro, "MOD", code_lines, cond))

        pre_lines, public_lines, protected_lines, private_lines = [], [], [], []
        section_lines = {"pre": pre_lines, "public": public_lines,
                          "protected": protected_lines, "private": private_lines}
        for modifier, _arg in c.resolved_modifiers:
            for section, line in modifier.gen_body_lines(c, cls_ctx):
                section_lines[section].append(line)

        body = []
        body.append("friend class ClassDB;")
        body.append("struct _class_type {};")
        body.append("template <class T> friend void has_bind_method(const T& t);")
        body.extend(pre_lines)
        body.append("public:")
        body.append(f'\tconstexpr static StaticString get_class_static() {{ return "{c.name}"_ss; }}')
        body.append(f'\tconstexpr static StaticString get_parent_name() {{ return "{c.parent}"_ss; }}')
        # A value type gets no virtual member at all -- that's the point: no
        # vtable, so e.g. an ECS component keeps its exact plain-struct size.
        # Runtime type queries go through ClassInfo instead of is_of_type().
        if not c.is_value_type:
            body.append("\tbool is_of_type(StaticString type_name) const override "
                        "{ return get_class_static() == type_name || Super::is_of_type(type_name); }")
            body.append("\tvirtual StaticString get_class_name() override { return get_class_static(); }")
        body.extend(public_lines)
        body.append("protected:")
        body.append(f"\tusing Type = {c.name};")
        # A rootless type (only possible for a value type) has no base clause
        # to alias -- `using Super = ;` would be a syntax error.
        if c.parent:
            body.append(f"\tusing Super = {c.parent};")
        body.append("\tstatic void _bind_members();")
        body.extend(protected_lines)

        # Generated accessors, grouped by access level; within a group,
        # unconditional lines are emitted directly (unchanged from before),
        # and conditional lines are grouped by their (already-flattened)
        # condition text into one hoisted helper macro per unique condition.
        for acc in ("public", "protected", "private"):
            group = [(code, cond) for (a, code, cond) in c.gen_getters if a == acc]
            if not group:
                continue
            body.append(f"{acc}:")
            body.extend(code for code, cond in group if cond is None)
            by_condition: dict = {}
            for code, cond in group:
                if cond is not None:
                    by_condition.setdefault(cond, []).append(code)
            for cond, code_lines in by_condition.items():
                body.append(hoist(macro, acc, code_lines, cond))
        if private_lines:
            body.append("private:")
            body.extend(private_lines)
        # Restore the type's starting access level, or a `private:` tail would
        # silently privatize every field declared below FSTRUCT() in a struct.
        body.append("public:" if c.tag == "struct" else "private:")

        # Emit as a single function-like macro with line continuations.
        lines.append(f"#define {macro}() \\")
        lines.append(" \\\n".join(body))
        lines.append("")

    if helper_blocks:
        lines[helper_insert_at:helper_insert_at] = helper_blocks

    return "\n".join(lines) + "\n"


def _bind_members_body(c: ClassDesc, dir_name: str) -> list:
    # Unlike the .gen.h macro, this is a real function body -- #if/#endif can
    # wrap a conditional binding call directly, no hoisted-helper-macro
    # indirection needed.
    def emit(out: list, condition: str, stmt: str):
        if condition:
            out.append(f"#if {condition}")
            out.append(stmt)
            out.append("#endif")
        else:
            out.append(stmt)

    out = [f"void {c.name}::_bind_members() {{"]
    for p in c.properties:
        ga = ACCESS_ENUM[p.getter_access]
        sa = ACCESS_ENUM[p.setter_access]
        has_get = p.getter_kind != "none"
        has_set = p.setter_kind != "none"
        if has_get and has_set:
            emit(out, p.condition, f"\tClassDB::bind_property_accessors_if_bindable("
                 f"&{c.name}::{p.getter_method}, &{c.name}::{p.setter_method}, "
                 f'"{p.prop_name}", {ga}, {sa});')
        elif has_get:
            emit(out, p.condition, f"\tClassDB::bind_property_get_if_bindable("
                 f'&{c.name}::{p.getter_method}, "{p.prop_name}", {ga});')
        elif has_set:
            emit(out, p.condition, f"\tClassDB::bind_property_set_if_bindable("
                 f'&{c.name}::{p.setter_method}, "{p.prop_name}", {sa});')
    for m in c.methods:
        acc = ACCESS_ENUM[m.access]
        if m.is_static:
            emit(out, m.condition, f'\tClassDB::bind_static_method(&{c.name}::{m.name}, "{m.bind_name}", {acc});')
        else:
            bind = "bind_method" if m.strict else "bind_method_if_bindable"
            emit(out, m.condition, f'\tClassDB::{bind}(&{c.name}::{m.name}, "{m.bind_name}", {acc});')
    ctx = EmitContext(dir_name=dir_name)
    for modifier, _arg in c.resolved_modifiers:
        for stmt in modifier.bind_members_lines(c, ctx):
            out.append(f"\t{stmt}")
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


def generate_register_cpp(subfolder: str, classes: list, core_path: Path, dir_emissions: list) -> str:
    func = f"register_{subfolder}_types"
    ordered = topological_sort(classes)
    ctx = EmitContext(dir_name=subfolder)

    includes, seen = [], set()
    for c in ordered:
        rel = _relative_include(c.header, core_path)
        if rel not in seen:
            seen.add(rel)
            includes.append(rel)
    for c in ordered:
        for modifier, _arg in c.resolved_modifiers:
            for inc in modifier.register_cpp_includes(c, ctx):
                if inc not in seen:
                    seen.add(inc)
                    includes.append(inc)
    for de in dir_emissions:
        for inc in de.cpp_includes:
            if inc not in seen:
                seen.add(inc)
                includes.append(inc)

    lines = [GENERATED_NOTICE,
             f'#include "register_{subfolder}_types.gen.h"',
             "#include <core/main/class_db.h>",
             ""]
    for inc in includes:
        lines.append(f'#include "{inc}"')
    lines += ["", "namespace feather {", ""]

    for c in ordered:
        lines.extend(_bind_members_body(c, subfolder))
        lines.append("")
        for modifier, _arg in c.resolved_modifiers:
            defs = modifier.register_cpp_definitions(c, ctx)
            if defs:
                lines.extend(defs)
                lines.append("")

    lines.append(f"void {func}() {{")
    if ordered:
        for c in ordered:
            # A modifier may replace the default register_class/register_value_class
            # call entirely (e.g. singleton -> register_singleton_class); the first
            # applied modifier (in resolved/sorted order) that opts in wins.
            stmts = None
            for modifier, _arg in c.resolved_modifiers:
                stmts = modifier.register_statements(c, ctx)
                if stmts is not None:
                    break
            if stmts is None:
                # No Reflected base for a value type, so no factory and no method
                # dispatch -- register_value_class binds properties (and static
                # methods) only.
                reg = "register_value_class" if c.is_value_type else "register_class"
                stmts = [f"ClassDB::{reg}<{c.name}>();"]
            for stmt in stmts:
                lines.append(f"\t{stmt}")
    else:
        lines.append("\t// No reflected classes found in this module.")
    lines += ["}", ""]

    for de in dir_emissions:
        lines.extend(de.cpp_lines)

    lines += ["} // namespace feather", ""]
    return "\n".join(lines) + "\n"


def generate_register_header(subfolder: str, dir_emissions: list) -> str:
    func = f"register_{subfolder}_types"

    extra_includes, seen = [], set()
    for de in dir_emissions:
        for inc in de.header_includes:
            if inc not in seen:
                seen.add(inc)
                extra_includes.append(inc)

    lines = [GENERATED_NOTICE, "#pragma once", ""]
    for inc in extra_includes:
        lines.append(f'#include "{inc}"')
    if extra_includes:
        lines.append("")
    lines += ["namespace feather {", "", f"void {func}();", ""]
    for de in dir_emissions:
        lines.extend(de.header_decls)
    lines += ["} // namespace feather", ""]
    return "\n".join(lines) + "\n"


def validate_hierarchy(classes: list):
    """A value type and a Reflected-derived type can't share an inheritance
    chain (one ends up overriding an is_of_type() that doesn't exist, or
    missing one that's pure virtual) -- both real compile errors, but they'd
    surface deep inside a generated macro expansion, so catch them here where
    we can name both classes. Only cross-checks classes in the *same* source
    dir; a chain crossing directories is left to the compiler."""
    known = {c.name: c for c in classes}
    for c in classes:
        base = known.get(c.parent)
        if base is None or base.is_value_type == c.is_value_type:
            continue
        kind, base_kind = ("a value type", "not") if c.is_value_type else ("not a value type", "is")
        raise ParseError(
            f"{c.header}:{c.fclass_line}: class {c.name} is {kind} (FSTRUCT/novtable) but its "
            f"parent {base.name} ({base.header}:{base.fclass_line}) {base_kind} -- a vtable-free "
            f"type and a Reflected-derived type can't be in the same inheritance chain. Use the "
            f"same form for both."
        )


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
    """Return [(offset, line, raw_args, macro), ...] for each real FCLASS(...)
    or FSTRUCT(...) in the (already comment/literal-blanked) text, skipping the
    macro definition lines in reflection_macros.h. Comment occurrences (e.g.
    reflection_macros.h's own "//   FCLASS()  plain reflected class"
    doc-comment) never reach here at all — blank_comments_and_literals()
    already erased them before this runs."""
    out = []
    for m in _FCLASS_RE.finditer(text):
        line_start = text.rfind("\n", 0, m.start()) + 1
        prefix = text[line_start:m.start()]
        # skip "#define FCLASS(...)" style definition lines
        if "#define" in prefix:
            continue
        line = text.count("\n", 0, m.start()) + 1
        out.append((m.start(), line, m.group(2), m.group(1)))
    return out


def process_header(header: Path, project_root: Path, registry: Registry, dir_name: str) -> list:
    # newline="" disables universal-newline translation: on a CRLF checkout,
    # Path.read_text()'s "\r\n" -> "\n" translation would silently collapse
    # one byte per line, making offsets increasingly diverge from on-disk
    # bytes -- observed as PBRMaterial (last class in material.h) silently
    # missing its GEN_BODY macro on a CRLF Windows checkout while earlier
    # classes in the same file still resolved. Path.open()'s newline param
    # works on all Python 3 versions, unlike Path.read_text()'s (3.13+ only).
    raw = header.open(encoding="utf-8", errors="replace", newline="").read()
    text = blank_comments_and_literals(raw)
    occ = scan_fclass_occurrences(text)
    if not occ:
        return []

    classes = []
    for name, parent, tag, body_start, body_end, _decl_line in find_class_bodies(text):
        body = text[body_start:body_end]
        cls = build_class(name, parent, tag, body, header, occ, body_start, body_end, registry, dir_name)
        if cls is not None:
            classes.append(cls)
    return classes


def find_headers(folder: Path):
    return sorted(folder.rglob("*.h")) + sorted(folder.rglob("*.hpp"))


_BUILTIN_EXTENSIONS_DIR = Path(__file__).resolve().parent / "extensions"


def build_registry(dir_path: Path, extra_extensions: list) -> Registry:
    """Built-in modifiers (tools/codegen/extensions/*.py) always load. Each
    (scope, path) in extra_extensions (--extension) additionally loads only
    when dir_path falls inside scope, so a project extension can never change
    what core/ (or an unrelated module) generates -- see modifier_api.py."""
    registry = Registry()
    if _BUILTIN_EXTENSIONS_DIR.is_dir():
        try:
            load_extension_path(registry, _BUILTIN_EXTENSIONS_DIR)
        except ModifierError as exc:
            print(f"[ERROR] loading built-in extensions: {exc}", file=sys.stderr)
            sys.exit(1)
    resolved_dir = dir_path.resolve()
    for scope, path in extra_extensions:
        if resolved_dir == scope or scope in resolved_dir.parents:
            try:
                load_extension_path(registry, path)
            except ModifierError as exc:
                print(f"[ERROR] loading extension {path}: {exc}", file=sys.stderr)
                sys.exit(1)
    return registry


def process_source_dir(dir_path: Path, name: str, include_base: Path, project_root: Path,
                        extra_extensions: list) -> int:
    """Scan dir_path recursively for FCLASS headers, emit each header's .gen.h,
    and emit dir_path/register_<name>_types.gen.{h,cpp}. Shared by the core-
    subfolder loop and the --module-path loop in main() -- a module dir is
    processed exactly like a core subfolder. include_base is what generated
    #include paths are computed relative to (core_path for core, the module
    dir itself for modules, so e.g. "vex_renderer.h" not
    "modules/vex_renderer/vex_renderer.h"). Returns files-written count."""
    changed = 0
    registry = build_registry(dir_path, extra_extensions)
    classes_by_header: dict = {}
    all_classes: list = []
    for header in find_headers(dir_path):
        try:
            classes = process_header(header, project_root, registry, name)
        except ParseError as exc:  # an annotated declaration we couldn't parse -- fatal
            print(f"[ERROR] {exc}", file=sys.stderr)
            sys.exit(1)
        if classes:
            classes_by_header[header] = classes
            all_classes.extend(classes)

    try:
        validate_hierarchy(all_classes)
    except ParseError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        sys.exit(1)

    dir_ctx = EmitContext(dir_name=name)
    dir_emissions = []
    for modifier in registry.modifiers():
        try:
            de = modifier.emit_dir(all_classes, dir_ctx)
        except ModifierError as exc:
            print(f"[ERROR] {dir_path}: modifier '{modifier.name}'.emit_dir: {exc}", file=sys.stderr)
            sys.exit(1)
        if de:
            dir_emissions.append(de)

    # Per-header gen.h files
    for header, classes in classes_by_header.items():
        gen_h = header.with_name(header.stem + ".gen.h")
        if write_if_changed(gen_h, generate_gen_header(classes, header, project_root, registry, name)):
            changed += 1
            print(f"  [{name}] updated {gen_h.name}")

    # register_<name>_types.gen.{h,cpp} (always emitted so the caller has the symbol)
    out_h = dir_path / f"register_{name}_types.gen.h"
    out_cpp = dir_path / f"register_{name}_types.gen.cpp"
    if write_if_changed(out_h, generate_register_header(name, dir_emissions)):
        changed += 1
        print(f"  [{name}] updated {out_h.name}")
    if write_if_changed(out_cpp, generate_register_cpp(name, all_classes, include_base, dir_emissions)):
        changed += 1
        print(f"  [{name}] updated {out_cpp.name}")
    return changed


def _parse_extension_arg(raw: str) -> tuple:
    """--extension scope=path -- scope is a source dir (or an ancestor of one)
    the extension applies to; path is a .py file or a directory of them. Both
    sides are resolved to absolute paths so scope-matching in build_registry
    doesn't depend on the caller's cwd."""
    if "=" not in raw:
        print(f"[ERROR] --extension expects 'scope=path', got: {raw!r}", file=sys.stderr)
        sys.exit(1)
    scope, _, path = raw.partition("=")
    scope_path = Path(scope).resolve()
    ext_path = Path(path).resolve()
    if not ext_path.exists():
        print(f"[ERROR] --extension {raw!r}: path not found: {ext_path}", file=sys.stderr)
        sys.exit(1)
    return (scope_path, ext_path)


def _parse_module_path_arg(raw: str) -> tuple:
    """--module-path DIR, or --module-path NAME=DIR to override the generated
    name (defaults to the dir's basename, which is wrong for a generically
    named dir like a project's src/). Returns (dir, name)."""
    name, sep, dir_part = raw.partition("=")
    if not sep:
        mod_path = Path(raw).resolve()
        return (mod_path, mod_path.name)
    if not name or not dir_part:
        print(f"[ERROR] --module-path expects 'DIR' or 'NAME=DIR', got: {raw!r}", file=sys.stderr)
        sys.exit(1)
    return (Path(dir_part).resolve(), name)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core-path", type=Path, default=Path("./core"))
    ap.add_argument("--project-root", type=Path, default=Path("."))
    ap.add_argument("--module-path", action="append", default=[], dest="module_paths",
                    help="'DIR' or 'NAME=DIR': additional (non-core) source dir to scan "
                         "(repeatable). NAME overrides the generated register_<NAME>_types "
                         "name, which otherwise comes from DIR's basename -- needed when the "
                         "dir is generically named (a project's src/).")
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
    ap.add_argument("--extension", action="append", default=[], dest="extensions",
                    help="'scope=path': load the FCLASS/FSTRUCT modifier extension at path "
                         "(a .py file or a directory of them) only when processing scope or a "
                         "directory beneath it. Repeatable. See xmake/modules/feather_codegen.lua's "
                         "add_extension() for how a project's own xmake.lua wires this in.")
    args = ap.parse_args()

    core_path = args.core_path.resolve()
    project_root = args.project_root.resolve()
    extra_extensions = [_parse_extension_arg(raw) for raw in args.extensions]

    total_changed = 0

    if not args.skip_core:
        if not core_path.is_dir():
            print(f"[ERROR] core path not found: {core_path}", file=sys.stderr)
            sys.exit(1)
        subfolders = [e.name for e in sorted(core_path.iterdir()) if e.is_dir()]
        for sub in subfolders:
            folder = core_path / sub
            total_changed += process_source_dir(folder, sub, core_path, project_root, extra_extensions)

    for mp in args.module_paths:
        mod_path, mod_name = _parse_module_path_arg(mp)
        if not mod_path.is_dir():
            print(f"[WARN] module path not found: {mod_path}", file=sys.stderr)
            continue
        # Named after the directory itself by default, e.g.
        # modules/vex_renderer/register_vex_renderer_types.gen.{h,cpp};
        # NAME=DIR overrides that (see _parse_module_path_arg).
        total_changed += process_source_dir(mod_path, mod_name, mod_path, project_root, extra_extensions)

    print(f"Done ({total_changed} file(s) updated).")


if __name__ == "__main__":
    main()
