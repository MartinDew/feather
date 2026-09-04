# feather_gen_cpp

Generates header-only C++ wrappers from the machine-readable description
`mrbind_gen_c --output-desc-json` writes, the same input `mrbind_gen_csharp`
reads. The result lets a C++ plugin call the engine through its C bindings and
nothing else: no engine headers, no engine checkout, no C++ ABI.

```
core/**.h --mrbind--> api.json --mrbind_gen_c--> feather_c/*.h + desc.json
                                                                    |
                                                  feather_gen_cpp <-+
                                                          |
                                                          v
                                                   feather_cpp/*.hpp
```

## How it is built

This is not a standalone CMake project. `thirdparty/packages/mrbind.lua` and
`tools/SDK/packages/mrbind_generators.lua` copy this directory into the mrbind
source tree they fetch, as `feather_gen_cpp/`, and append
`add_subdirectory(feather_gen_cpp)` to mrbind's own `CMakeLists.txt`. Everything
mrbind sets at directory scope — `-std=c++23`, `_ITERATOR_DEBUG_LEVEL=0`,
`CMAKE_MSVC_RUNTIME_LIBRARY`, the cppdecl include path — then applies here too,
which is what makes linking `mrbind_c_interop` work on every toolchain the
engine supports.

Both packages also apply the source edit documented in
`patches/expose-as-struct-standard-layout-bases.md`.

## Rebuilding after editing these sources

The packages hash this directory into a `gen_cpp_rev` config, so an edit here
changes the package's install hash and xmake reinstalls it on the next
configure. If a stale binary is ever suspected:

```
xmake require --force mrbind_generators   # or mrbind, for the engine-side build
```

## Usage

```
feather_gen_cpp --input-json <desc.json> --output-dir <dir> [--clean-output-dir]
                [--c-dir-prefix feather_c] [--cpp-dir-prefix feather_cpp]
                [--native-type <cpp_type> <header>]...
```

`--native-type` declares that the consumer already has a bit-identical
definition of a C++ type (the vendored SimpleMath types), so the generator
aliases it instead of emitting a wrapper class, and asserts the layout the
descriptor recorded.
