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
assumes well-formed, not-too-exotic C++ (no raw string literals, no digraphs).

A reflected member may sit inside #if/#ifdef/#ifndef/#elif/#else/#endif —
arbitrarily nested, with arbitrary condition text (negation, &&/||, defined(),
...), since that text is only ever copied, never evaluated. See
iter_conditioned_chunks() for how the #if-stack is tracked and
generate_gen_header()/_bind_members_body() for how the *same* condition is
reproduced around the generated code, so one .gen.h is correct for every
build config (no per-config regeneration).

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

# A preprocessor directive line: '#' then a keyword, only ever matched right
# at the start of a (whitespace-stripped) logical line -- see iter_member_chunks.
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
    modifiers: set = dc_field(default_factory=set)
    is_singleton: bool = False
    is_abstract: bool = False
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
    for each other depth-0 member chunk in body (body is class-body text,
    strictly between the class's { and }; offset is relative to body's own
    start).

    A preprocessor directive ('#' as the first non-whitespace token on a
    logical line, any number of backslash-continued physical lines) is
    recognized and consumed as its own zero-content unit *before* the member-
    chunk scan ever gets a chance to run into it -- this matters: without it,
    a directive sitting between two real declarations (e.g. an '#if EDITOR_
    BUILD' guarding one member) would otherwise get glued onto whatever
    non-terminated text follows it into one garbled chunk, silently losing a
    leading [[...]] attribute inside that chunk (classify_chunk only ever
    reads a *leading* attribute) and, if an access specifier happens to fall
    inside the same swallowed span, desyncing the access-level tracking for
    every member after it. keyword/rest are handed to iter_conditioned_chunks
    to build up the #if/#ifdef/#ifndef/#elif/#else/#endif stack; this
    function itself has no opinion on what the directives mean.

    A member chunk is the text up to a depth-0 ';', or up to (and including a
    ';' immediately following, if any) the matching '}' of a depth-0 brace
    block — this covers plain declarations, inline method bodies, and default
    member initializers that themselves contain balanced (), [], {} (e.g.
    `Color _c = Color(1.0f, 1.0f, 1.0f, 1.0f);`). A nested class/struct
    definition is swallowed whole into a single opaque chunk by the same rule
    (its '{' opens depth 1 same as any other brace block) — its members,
    including any [[...]] they carry, are never seen as this class's own
    members, since only a chunk's *leading* attribute is ever read (see
    classify_chunk). Directives *inside* a single declaration (e.g. splitting
    a parameter list) aren't supported — real code doesn't do this for
    reflected members, and it's not worth the complexity."""
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
    """One #if/#ifdef/#ifndef...#endif nesting level. chain is a unique id
    per opening directive, shared by every #elif/#else branch that follows it
    (so sibling branches of the same chain can be recognized as mutually
    exclusive); branch counts up from 0 for each. own_text is this branch's
    own condition text (already normalized: #ifdef X -> "defined(X)", #ifndef
    X -> "!defined(X)", #if/#elif kept verbatim) -- None for an #else branch,
    whose effective condition is only known once every prior sibling's own
    text has been collected (see effective_text)."""
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
    """True if path_a and path_b (each a tuple of (chain, branch) — one entry
    per #if-region enclosing a member, outermost first, as produced by
    iter_conditioned_chunks) are *provably* mutually exclusive: they're
    nested identically up to some #if-chain, where they land in different
    branches of that same chain (the "#if X ... #else ... #endif" duplicate-
    impl idiom). Only that direct-sibling-branches pattern is recognized —
    two unrelated/independent conditions (different chains, even if the
    condition text happens to look similar) are conservatively treated as
    NOT provably exclusive, same as the old plain name-count check treated
    every same-named occurrence. This never affects unconditional code: two
    empty paths compare as not-exclusive, matching the pre-existing
    "duplicate name -> ambiguous, skip" behavior exactly."""
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
    condition is None when the member is unconditional, else the flattened,
    fully-parenthesized '(A) && (B)' boolean-expression text of every
    enclosing region, verbatim from source (never interpreted -- negation,
    &&/||, defined(), arbitrary nesting all just come along for the ride
    since it's plain text copy-and-conjoin, not something evaluated). Each
    #if/#ifdef/#ifndef opens one frame; #elif/#else advance the top frame in
    place (same chain, new branch); #endif closes it. Raises ParseError if
    the stack isn't back to empty at the end of body (unbalanced #if/#endif)
    -- that's a shape this scanner can't safely reason about, independent of
    whether any annotated member was involved.
    cond_path is that same stack reduced to (chain, branch) pairs, used only
    by build_class() to recognize when two same-named methods live in
    mutually exclusive branches of one #if-chain (see _mutually_exclusive)."""
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

    try:
        # First pass: tally method names by (source offset, cond_path)
        # (constructors/destructors excluded, matching the old clang
        # backend's CXXMethodDecl-only tally) so overloaded names can be
        # excluded from binding below — &T::name would be ambiguous for an
        # overload, whether or not the overload itself is annotated. Keyed
        # by offset (not just name) so a method can be compared against every
        # *other* same-named occurrence without matching itself; cond_path
        # lets _handle_method recognize the common "#if X ... #else ... #endif"
        # duplicate-impl idiom as NOT actually ambiguous (see _mutually_exclusive).
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
    plan = PropertyPlan(prop_name=prop, member_name=member, type_spelling=type_spelling, condition=condition)

    if not present_get and not present_set:
        present_get = present_set = True
        get_arg = set_arg = None
    else:
        get_arg = attrs.get("get")
        set_arg = attrs.get("set")

    if present_get:
        _plan_accessor(plan, "get", get_arg, access, condition, cls)
    if present_set:
        _plan_accessor(plan, "set", set_arg, access, condition, cls)

    if plan.getter_kind != "none" or plan.setter_kind != "none":
        cls.properties.append(plan)


def _plan_accessor(plan: PropertyPlan, which: str, arg, member_access: str, condition: str, cls: ClassDesc):
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
    # Methods are opt-in: only [[method]] (optionally [[method(name)]] to rebind
    # under a custom reflected name) binds a method, whether static or not.
    forced = "method" in attrs
    if not forced:
        return
    # Skip templated/overloaded names: &T::name would be ambiguous. Overloads
    # must be disambiguated by hand rather than auto-bound -- UNLESS every
    # other same-named occurrence is provably in a mutually exclusive #if
    # branch of the same chain (the common "#if X ... #else ... #endif"
    # duplicate-impl idiom), in which case only one of them is ever actually
    # compiled and &T::name is never really ambiguous.
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
    helper_insert_at = len(lines)

    # #if/#endif cannot be spliced into a backslash-continued macro body: line
    # -splicing (translation phase 2) happens before directives are recognized
    # (phase 3/4), so a "#if"/"#endif" written as a continuation line of a
    # #define is just literal '#' 'if' text in the macro's replacement list,
    # not a directive -- FCLASS() would expand to a syntax error. So a
    # conditional accessor's code is instead hoisted into its own top-level
    # macro, guarded by a REAL #if/#else/#endif (one physical line each, never
    # spliced into anything), and the main GEN_BODY macro merely references
    # that macro's bare name -- which *does* get expanded normally on rescan
    # when GEN_BODY() itself is invoked, after every #define in this file has
    # already been processed. One .gen.h is then correct for every build
    # config; no per-config regeneration needed.
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
        body.append("private:")

        # Emit as a single function-like macro with line continuations.
        lines.append(f"#define {macro}() \\")
        lines.append(" \\\n".join(body))
        lines.append("")

    if helper_blocks:
        lines[helper_insert_at:helper_insert_at] = helper_blocks

    return "\n".join(lines) + "\n"


def _bind_members_body(c: ClassDesc) -> list:
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
