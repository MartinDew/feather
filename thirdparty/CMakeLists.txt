include(FetchContent)

set(THIRDPARTY_PACKAGES "")
set(THIRDPARTIES "")

# include all subfolders configure.cmake files
file(GLOB_RECURSE CONFIG_FILES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "*configure.cmake")
foreach(CONFIG_FILE ${CONFIG_FILES})
    include(${CMAKE_CURRENT_SOURCE_DIR}/${CONFIG_FILE})
endforeach()

FetchContent_MakeAvailable(${THIRDPARTY_PACKAGES})

set(THIRDPARTIES ${THIRDPARTIES} PARENT_SCOPE)