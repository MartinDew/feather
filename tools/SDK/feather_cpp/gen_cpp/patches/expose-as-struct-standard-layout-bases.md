# mrbind edit: allow `--expose-as-struct` on standard-layout types with bases

Applied in `on_install` by both `thirdparty/packages/mrbind.lua` and
`tools/SDK/packages/mrbind_generators.lua`, against the pinned commit
`232ff33159d5e76e57b11669453d7d25ad22a14d`.

## What is removed

`src/generators/c/generator.cpp`, immediately after the trivially-copyable and
standard-layout checks:

```cpp
                // Must have no bases. I ain't dealing with those.
                if (!class_info.parsed->bases.empty())
                    throw std::runtime_error("The class `" + cpp_type_name + "` is whitelisted by `--expose-as-struct`, but it has a base class. This flag only supports the structs/classes with no base classes.");
```

## Why it is safe

The check sits behind two that stay. `std::is_standard_layout` already
guarantees that at most one class in the hierarchy declares non-static data
members and that the base subobject shares the address of the complete object,
so a base contributes fields at their own recorded offsets and nothing else.
`std::is_trivially_copyable` rules out anything with a non-trivial special
member.

The emitter then re-derives the layout and validates it: `EmitExposedStruct()`
checks every field's size, alignment and byte offset against the values the
parser recorded, and the struct's total size and alignment against the parsed
type's, throwing on any disagreement. A hierarchy this edit wrongly admits still
fails loudly rather than producing a mismatched struct.

Base fields reach the emitter only because the parse passes
`--copy-inherited-members`, which copies each base `ClassField` with its
`byte_offset` intact.

## Why an edit rather than `add_patches`

The package pins a commit rather than a version, so there is no version key for
`add_patches` to match, and a plain-text replacement needs no `patch` binary on
Windows. Both packages assert the text is gone afterwards, so an upstream move
fails the build instead of silently skipping the edit.

## Why this is wanted

`DirectX::SimpleMath::Vector2/3/4`, `Quaternion` and `Color` derive from
`XMFLOAT2/3/4`, which is where their fields live. Without this they cannot be
exposed as real C structs, and the engine's math API cannot cross the C boundary
by value.
