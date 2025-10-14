include(FetchContent)

# include all subfolders configure.cmake files
file(GLOB_RECURSE CONFIG_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*configure.cmake")
include(${CONFIG_FILES})

set(THIRDPARTIES
    ${FETCHED_TPS}
    PARENT_SCOPE)

FetchContent_MakeAvailable(${FETCHED_TPS})