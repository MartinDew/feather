FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG e6987e2452aa8619d4174ac06bb465e36de337c9 # release 3.2.x
        # EXCLUDE_FROM_ALL
)

set(THIRDPARTY_PACKAGES SDL3)
set(THIRDPARTIES SDL3::SDL3 PARENT_SCOPE)

# if release build then use static linking
IF (CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SDL_SHARED OFF CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Use shared SDL library" FORCE)
ELSE()
    set(SDL_SHARED ON CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Use shared SDL library" FORCE)
ENDIF()
