# feather_gen_cpp: the C++ wrapper generator, built inside mrbind's own CMake
# tree rather than as a standalone project.
#
# The xmake packages that build mrbind (thirdparty/packages/mrbind.lua and
# tools/SDK/packages/mrbind_generators.lua) copy this directory into the fetched
# mrbind source as feather_gen_cpp/ and append an add_subdirectory() for it. That
# grafting is deliberate: mrbind's top-level CMakeLists sets -std=c++23,
# _ITERATOR_DEBUG_LEVEL=0 and CMAKE_MSVC_RUNTIME_LIBRARY at directory scope, and
# a target that does not inherit all three fails to link against mrbind_c_interop
# (LNK2038 on Windows). A wrapper project that add_subdirectory'd mrbind would
# not inherit them.

# The interop library only exists when a generator was enabled; both packages
# pass MRBIND_BUILD_GENERATOR_C=ON, so its absence means the graft moved.
if(NOT TARGET mrbind_c_interop)
    message(FATAL_ERROR "feather_gen_cpp: mrbind_c_interop target not found -- this directory must be add_subdirectory()'d from mrbind's own CMakeLists, after its generator targets.")
endif()

# The executable is written to the build root (see RUNTIME_OUTPUT_DIRECTORY
# below), so this directory's own build folder must not be named after it, or
# the linker's output path is a directory. Both packages therefore append
# `add_subdirectory(feather_gen_cpp _feather_gen_cpp_build)`.
if(CMAKE_CURRENT_BINARY_DIR STREQUAL "${CMAKE_BINARY_DIR}/feather_gen_cpp")
    message(FATAL_ERROR "feather_gen_cpp: this directory's binary dir collides with the executable's own path. Add it as `add_subdirectory(feather_gen_cpp _feather_gen_cpp_build)`.")
endif()

add_executable(feather_gen_cpp
    src/emit.cpp
    src/generator.cpp
    src/main.cpp
)

target_include_directories(feather_gen_cpp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps/cppdecl/include
)

# PUBLIC-links mrbind_common and mrbind_gen_common, which is where the desc
# model, its JSON loader and the command-line parser live.
target_link_libraries(feather_gen_cpp PRIVATE
    mrbind_c_interop
)

# Both packages copy binaries out of the build root by name; keep this one
# beside mrbind_gen_c rather than in a per-target subdirectory.
set_target_properties(feather_gen_cpp PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

if(MRBIND_STATIC_BUILD)
    apply_static_stdlib(feather_gen_cpp)
endif()
