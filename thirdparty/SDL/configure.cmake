FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG e6987e2452aa8619d4174ac06bb465e36de337c9 # release 3.2.x
        EXCLUDE_FROM_ALL
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

list(APPEND THIRDPARTY_PACKAGES SDL3)
list(APPEND THIRDPARTIES SDL3::SDL3)

# if release build then use static linking
IF (${STATIC_CPP})
    set(SDL_SHARED OFF CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC ON CACHE BOOL "Use shared SDL library" FORCE)
ELSE()
    set(SDL_SHARED ON CACHE BOOL "Use shared SDL library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Use shared SDL library" FORCE)
ENDIF()

set(SDL_MAIN OFF)
set (SDL_AUDIO    OFF)
set (SDL_VIDEO    ON)
set (SDL_GPU      OFF)
set (SDL_RENDER   OFF)
set (SDL_CAMERA   OFF)
set (SDL_JOYSTICK OFF)
set (SDL_HAPTIC   OFF)
set (SDL_HIDAPI   OFF)
set (SDL_POWER    OFF)
set (SDL_SENSOR   OFF)
set (SDL_DIALOG   OFF)
