cmake_minimum_required(VERSION 3.15)
project(Feather VERSION 1.0.0 LANGUAGES CXX)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(tools/options.cmake)

add_subdirectory(thirdparty)

# Set C++ standard
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configuration types for multi-config generators
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Development" CACHE STRING "" FORCE)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(PROJECT_HEADERS
    core/launch_settings.h
    core/window.h
    core/engine.h
)

# Source files (shared between both targets)
set(PROJECT_SOURCES
    core/main.cpp
    core/launch_settings.cpp
    core/window.cpp
    core/engine.cpp
 )

# Editor target
add_executable(Editor ${PROJECT_SOURCES} ${PROJECT_HEADERS})

target_include_directories(Editor PRIVATE core)
target_compile_definitions(Editor PRIVATE EDITOR_MODE)
target_link_libraries(Editor PUBLIC ${THIRDPARTIES})

# Compiler options for Editor
target_compile_options(Editor PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:Development>:-g -O2>
)

# Standalone target
add_executable(Standalone ${PROJECT_SOURCES} ${PROJECT_HEADERS})
target_include_directories(Standalone PRIVATE core)
target_compile_definitions(Standalone PRIVATE STANDALONE_MODE)
target_link_libraries(Standalone PRIVATE ${THIRDPARTIES})

# Compiler options for Standalone
target_compile_options(Standalone PRIVATE
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
    $<$<CONFIG:Development>:-g -O2>
)

# Platform-specific settings
if(MSVC)
    target_compile_options(Editor PRIVATE /W4 /wd4100)
    target_compile_options(Standalone PRIVATE /W4 /wd4100)
    
    # MSVC-specific config flags
    target_compile_options(Editor PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Development>:/O2 /Zi>
    )
    target_compile_options(Standalone PRIVATE
        $<$<CONFIG:Debug>:/Od /Zi>
        $<$<CONFIG:Release>:/O2 /DNDEBUG>
        $<$<CONFIG:Development>:/O2 /Zi>
    )
else()
    target_compile_options(Editor PRIVATE -Wall -Wextra -pedantic -Wno-unused-parameter)
    target_compile_options(Standalone PRIVATE -Wall -Wextra -pedantic -Wno-unused-parameter)
endif()

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")